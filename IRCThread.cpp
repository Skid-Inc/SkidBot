#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>
#include <deque>

#include "IRCThread.hpp"
#include "SkidBot.hpp"
#include "Logger.hpp"

// Function prototypes
int send_command (std::string command, std::string data);

// Global varibles
bool irc_running = true;
pthread_mutex_t irc_mutex = PTHREAD_MUTEX_INITIALIZER;

// Used for normal twitch IRC
uint8_t irc_task = 0;
int irc_return;
int irc_sock;
int irc_port = DEFAULT_IRC_PORT;
struct sockaddr_in irc_serv_addr;
hostent *irc_server;

std::deque<std::string> irc_recv_buffer (20);

// Used for groups twitch IRC
uint8_t girc_task = 0;
int girc_return;
int girc_sock;
int girc_port = DEFAULT_IRC_PORT;
struct sockaddr_in girc_serv_addr;
hostent *girc_server;

std::deque<std::string> girc_recv_buffer (20);

extern Logger *logger;


/**
 * IRCThread, handles connecting to twitch irc and parsing incoming messages.
 */
void *IRCThread (void *)
{
	char irc_recv[MAXDATAREAD];
	uint32_t available; 

	lock (irc_mutex);
	while (irc_running)
	{
		release (irc_mutex);
		switch (irc_task)
		{
			case (IRC_CONNECT):
			{
				irc_recv_buffer.empty ();
				irc_sock = socket(AF_INET, SOCK_STREAM, 0);
				irc_server = gethostbyname("irc.twitch.tv");
				if ((irc_sock >= 0) && (irc_server != NULL))
				{
					// Sets up the server address
					bzero((char *) &irc_serv_addr, sizeof(irc_serv_addr));
					irc_serv_addr.sin_family = AF_INET;
					bcopy((char *)irc_server->h_addr, (char *)&irc_serv_addr.sin_addr.s_addr, irc_server->h_length);
					irc_serv_addr.sin_port = htons(irc_port);

					irc_return = connect (irc_sock, (struct sockaddr *) &irc_serv_addr, sizeof (irc_serv_addr));
					if (irc_return >= 0)
					{
						irc_task = IRC_AUTH;
						logger->log (" IRCThread: I've connected to the IRC server.\n");
						break;
					}
					else
					{
						logger->logf (" IRCThread: I was unable to get the server host, reason: %s.\n", strerror(errno));
					}
				}
				else
				{
					logger->logf (" IRCThread: I was unable to open a socket, or find the server, reason: %s.\n", strerror(errno));
				}
			}
			break;

			case (IRC_AUTH):
			{
				irc_return = send_command ("PASS", "oauth:1234567890");
				irc_return = send_command ("USER", "SkidBot 0 0 :NathanSkidmore");
				irc_return = send_command ("NICK", "SkidBot");
				irc_return = send_command ("CAP REQ", ":twitch.tv/commands");
				irc_return = send_command ("CAP REQ", ":twitch.tv/membership");
				irc_return = send_command ("JOIN", "#n_skid11");

				logger->log (" IRCThread: I've successfully authorised myself on the server.\n");
				irc_task = IRC_RUNNING;
			}
			break;

			case (IRC_RUNNING):
			{
				irc_return = ioctl(irc_sock, FIONREAD, &available);
				if (available > 0)
				{
					memset (irc_recv, 0, MAXDATAREAD);
					irc_return = read(irc_sock, irc_recv, MAXDATAREAD);
					if (irc_return > 0)
					{
						std::string received (irc_recv);
						std::string message;
						size_t last_found = 0;
						size_t found = 0;

						found = received.find ("\r\n");
						while (found != std::string::npos)
						{
							message = received.substr(last_found, found-last_found);
							irc_recv_buffer.push_back(message);
							// TODO: Disable this debug message
							logger->debugf (DEBUG_DETAILED, " IRCThread: I received: %s\r\n", message.c_str());
							last_found = found+2;
							found = received.find ("\r\n", last_found);
						}
					}
					else if ((irc_return != -EAGAIN) && (irc_return != -EWOULDBLOCK) && (irc_return != -1) && (irc_return != 0))
					{
						// Failed to read the message
						logger->logf (" IRCThread: I had a problem reading the IRC socket, so I'm reconnecting, reason: %s.\n", strerror(errno));
						irc_task = IRC_CLOSE;
					}
				}
			}
			break;

			case (IRC_CLOSE):
			{
				send_command ("PART", "Bye Bye ^^");
				send_command ("QUIT", "SkidBot");
				close (irc_sock);
				irc_sock = -1;
				
				irc_task = IRC_CONNECT;
			}
			break;
		}

		usleep (100000);
		lock (irc_mutex);
	}
	release (irc_mutex);

	send_command ("PART", "Bye Bye ^^");
	send_command ("QUIT", "SkidBot");
	close (irc_sock);
	
	irc_recv_buffer.clear ();
	
	logger->log (" IRCThread: I've stopped the IRC thread.\n");

	return NULL;
}


/**
 * Sends a irc command to the server
 */
int send_command (std::string command, std::string data)
{
	int command_return;

	std::string message;

	message = command;
	if (!data.empty())
	{
		message.append (" ");
		message.append (data);
	}
	message.append ("\r\n");

	lock (irc_mutex);
	command_return = write (irc_sock, message.c_str(), message.size());
	release (irc_mutex);

	if (command_return < 0)
	{
		logger->logf (" IRCThread: I was unable to send the following message to the IRC server: %s, reason: %s.\n", message.c_str(), strerror(errno));
	}
	else
	{
		// TODO: Disable this debug message
		logger->debugf (DEBUG_MINIMAL, " IRCThread: I sent: %s", message.c_str());
	}

	return command_return;
}


/**
 * Sends a message to a given room
 */
int send_room (std::string room, std::string message)
{
	std::string output;
	output.append (room);
	output.append (" :");
	output.append (message);
	return send_command ("PRIVMSG", output);
}


/**
 * GIRCThread, handles connecting to twitch groups irc and parsing incoming messages.
 */
void *GIRCThread (void *)
{
	char girc_recv[MAXDATAREAD];
	uint32_t available;

	lock (irc_mutex);
	while (irc_running)
	{
		release (irc_mutex);
		switch (girc_task)
		{
			case (IRC_CONNECT):
			{
				girc_recv_buffer.empty ();
				girc_sock = socket(AF_INET, SOCK_STREAM, 0);
				girc_server = gethostbyname("199.9.253.119");
				if ((girc_sock >= 0) && (girc_server != NULL))
				{
					// Sets up the server address
					bzero((char *) &girc_serv_addr, sizeof(girc_serv_addr));
					girc_serv_addr.sin_family = AF_INET;
					bcopy((char *)girc_server->h_addr, (char *)&girc_serv_addr.sin_addr.s_addr, girc_server->h_length);
					girc_serv_addr.sin_port = htons(girc_port);

					girc_return = connect (girc_sock, (struct sockaddr *) &girc_serv_addr, sizeof (girc_serv_addr));
					if (girc_return >= 0)
					{
						girc_task = IRC_AUTH;
						logger->log (" GIRCThread: I've connected to the groups IRC server.\n");
						break;
					}
					else
					{
						logger->logf (" GIRCThread: I was unable to get the groups server host, reason: %s.\n", strerror(errno));
					}
				}
				else
				{
					logger->logf (" GIRCThread: I was unable to open a socket, or find the groups server, reason: %s.\n", strerror(errno));
				}
			}
			break;

			case (IRC_AUTH):
			{
				girc_return = gsend_command ("PASS", "oauth:1234567890");
				girc_return = gsend_command ("USER", "SkidBot 0 0 :NathanSkidmore");
				girc_return = gsend_command ("NICK", "SkidBot");
				girc_return = gsend_command ("JOIN", "#jtv");
				girc_return = gsend_command ("CAP REQ", ":twitch.tv/commands");

				logger->log (" GIRCThread: I've successfully authorised myself on the groups server.\n");
				girc_task = IRC_RUNNING;
			}
			break;

			case (IRC_RUNNING):
			{
				girc_return = ioctl(girc_sock, FIONREAD, &available);
				if (available > 0)
				{
					memset (girc_recv, 0, MAXDATAREAD);
					girc_return = read(girc_sock, girc_recv, MAXDATAREAD);
					if (girc_return > 0)
					{
						std::string received (girc_recv);
						std::string message;
						size_t last_found = 0;
						size_t found = 0;

						found = received.find ("\r\n");
						while (found != std::string::npos)
						{
							message = received.substr(last_found, found-last_found);
							girc_recv_buffer.push_back(message);
							logger->debugf (DEBUG_DETAILED, " GIRCThread: I received on groups: %s\r\n", message.c_str());
							last_found = found+2;
							found = received.find ("\r\n", last_found);
						}
					}
					else if ((girc_return != -EAGAIN) && (girc_return != -EWOULDBLOCK) && (girc_return != -1) && (girc_return != 0))
					{
						// Failed to read the message
						logger->logf (" GIRCThread: I had a problem reading the groups IRC socket, so I'm reconnecting, reason: %s.\n", strerror(errno));
						girc_task = IRC_CLOSE;
					}
				}
			}
			break;

			case (IRC_CLOSE):
			{
				gsend_command ("PART", "Bye Bye ^^");
				gsend_command ("QUIT", "SkidBot");
				close (girc_sock);
				girc_sock = -1;
				
				girc_task = IRC_CONNECT;
			}
			break;
		}

		usleep (100000);
		lock (irc_mutex);
	}
	release (irc_mutex);

	gsend_command ("PART", "Bye Bye ^^");
	gsend_command ("QUIT", "SkidBot");
	close (girc_sock);
	
	girc_recv_buffer.clear ();
	
	logger->log (" GIRCThread: I've stopped the groups IRC thread.\n");

	return NULL;
}


/**
 * Sends a irc command to the groups server
 */
int gsend_command (std::string command, std::string data)
{
	int command_return;

	std::string message;

	message = command;
	if (!data.empty())
	{
		message.append (" ");
		message.append (data);
	}
	message.append ("\r\n");

	lock (irc_mutex);
	command_return = write (girc_sock, message.c_str(), message.size());
	release (irc_mutex);

	if (command_return < 0)
	{
		logger->logf (" GIRCThread: I was unable to send the following message to the groups IRC server: %s, reason: %s.\n", message.c_str(), strerror(errno));
	}
	else
	{
		// TODO: Disable this debug message
		logger->debugf (DEBUG_MINIMAL, " GIRCThread: I sent on groups: %s", message.c_str());
	}

	return command_return;
}


/**
 * Sends a message to a given room on the groups server
 */
int gsend_room (std::string room, std::string message)
{
	std::string output;
	output.append (room);
	output.append (" :");
	output.append (message);
	return gsend_command ("PRIVMSG", output);
}
// g++ -std=c++11 -Wall *.cpp -lrt -lpthread -lboost_regex -lmysqlclient -lcurl -o SkidBot
// Could use libjson0-dev to parse the json

#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>
#include <deque>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include "SkidBot.hpp"
#include "Logger.hpp"
#include "MySQLHandler.hpp"
#include "IRCThread.hpp"
#include "TwitchAPIThread.hpp"

#define VERSION "0.31"

// Local function prototypes
void signalHandler (int signum);

// Global Varible
uint8_t debug_level = DEBUG_NONE;
Logger *logger;
MySQLHandler *mysql;
volatile sig_atomic_t closing_process = 0;
volatile sig_atomic_t close_reason = 0;
pthread_t irc_thread;
pthread_t girc_thread;
pthread_t tapi_thread;
extern uint8_t irc_task;
extern bool irc_running;
extern pthread_mutex_t irc_mutex;
extern bool tapi_running;
extern pthread_mutex_t tapi_mutex;

// Data stores
std::vector<std::string> users_chatted;		// Holds a list of users that have chatted in the stream

std::chrono::high_resolution_clock::time_point current_time;
std::chrono::high_resolution_clock::time_point anti_spam;
std::chrono::high_resolution_clock::time_point no_spoilers;
bool no_spoilers_running = false;

extern std::deque<std::string> irc_recv_buffer;
extern std::deque<std::string> girc_recv_buffer;

// API external variables
extern std::string current_title;
extern std::string current_game;

int main(int argc, char **argv)
{
	// Setup the logger and log the start of the process
	logger = new Logger ("SkidBot.log");
	logger->setLinePrefix ("SkidBot");
	logger->log (": Powering up... I'm version " VERSION " compiled on " __DATE__ ", " __TIME__ ".\n");

	// All close signals should be send to the signalHandler method
	signal (SIGTERM, &signalHandler);
	signal (SIGQUIT, &signalHandler);
	signal (SIGINT, &signalHandler);
	// Sends some program error signals to the signalHandler method
	signal (SIGILL, &signalHandler);
	signal (SIGSEGV, &signalHandler);
	signal (SIGBUS, &signalHandler);
	// All SIGCHLD signals are ignored, otherwise the child process becomes a zombie when closed
	signal (SIGCHLD, SIG_IGN);
	// ALL SIGPIPE signals are ignored, otherwise the program exits if it tries to write after the connection has dropped
	signal (SIGPIPE, SIG_IGN);

	// Processes command line arguments
	int arg_count = 1;
	for (arg_count = 1; arg_count < argc; arg_count++)
	{
		if (strcmp(argv[arg_count], "-d") == 0)
		{
			// Check to make a debug number was set
			if (argc - 1 >= arg_count + 1)
			{
				debug_level = atoi (argv[arg_count+1]);
				if (debug_level <= 3)
				{
					logger->logf (": Running in debug mode %d.\n", debug_level);
				}
				else
				{
					debug_level = 0;
					logger->log (": Unknown debug mode, I'm confused.\n");
				}
				logger->setDebugLevel (debug_level);
			}
			else
			{
				logger->log (": Why did you set the debug flag without the debug value?.\n");
			}
		}
	}

	// Starts the MySQL Handler
	mysql = new MySQLHandler (logger);

	// Creates the irc thread
	logger->log (": I'm starting my IRC thread so I can connect to twitch.\n");
	pthread_create (&irc_thread, NULL, IRCThread, NULL);
	sleep (2);
	
	// Creates the groups irc thread
	logger->log (": I'm starting my groups IRC thread so I can connect to twitch.\n");
	pthread_create (&girc_thread, NULL, GIRCThread, NULL);
	sleep (2);

	// Creates the twitch api thread
	logger->log (": I'm starting my Twitch API thread so I can monitor the channel.\n");
	pthread_create (&tapi_thread, NULL, TwitchAPIThread, NULL);
	sleep (1);

	current_time = hrc_now;
	anti_spam = current_time;
	no_spoilers_running = false;

	while (closing_process != 1)
	{
		if (irc_task == IRC_RUNNING)
		{
			while (irc_recv_buffer.size() > 0)
			{
				std::string message;
				std::string chat;
				std::string user;
				std::string room;
				size_t cmd_location;
				size_t user_location;
				size_t data_location;

				current_time = hrc_now;
				message = irc_recv_buffer.front();
				irc_recv_buffer.pop_front();

				// Otherwise looks for chat messages
				cmd_location = message.find ("PRIVMSG");
				if (cmd_location != std::string::npos)
				{
					// Try to get the user
					user_location = message.find ("!");
					if (user_location != std::string::npos)
					{
						user = message.substr(1, user_location - 1);
					}
					else
					{
						user = "Unknown";
					}


					// Try to get the message only
					data_location = message.find (":", cmd_location);
					if (data_location != std::string::npos)
					{
						// Get the room the message was in
						room = message.substr(cmd_location + 8, data_location - cmd_location - 9);

						if (message.substr(data_location + 1, 7).compare("\001ACTION") == 0)
						{
							chat = message.substr(data_location + 9);
							logger->logf (": I found a user action in room: %s, user: %s, action: %s\n", room.c_str(), user.c_str(), chat.c_str());
							
							// Checks if this user has posted before
							bool user_chatted = std::binary_search(users_chatted.begin(), users_chatted.end(), user);
							
							if ((!user_chatted) && (boost::regex_search (chat.c_str(), boost::regex("[^\\s.].[^\\s.]"))))
							{
								logger->logf (": Someone posted a link without having spoken in chat first, spam protection active.\n");
								std::string temp = "/timeout ";
								temp.append (user.c_str());
								temp.append (" 60");
								send_room (room, temp);
								send_room (room, "My master doesn't like spambots, he says spambots are bad.");
							}
							else
							{
								// If the user hasn't chatted before, add them to the list
								if (!user_chatted)
								{
									logger->logf (": %s posted their first message without a link, adding them to the list.\n", user.c_str());
									users_chatted.push_back (user);
									std::sort (users_chatted.begin(), users_chatted.end());
								}
							}
						}
						else
						{
							chat = message.substr(data_location + 1);
							logger->debugf (DEBUG_MINIMAL, ": I found a chat message in room: %s, user: %s, message: %s\n", room.c_str(), user.c_str(), chat.c_str());
							
							// Checks if this user has posted before
							bool user_chatted = std::binary_search(users_chatted.begin(), users_chatted.end(), user);
							
							if ((!user_chatted) && (boost::regex_search (chat.c_str(), boost::regex("[^\\s.]\\.[^\\s.]"))))
							{
								logger->logf (": Someone posted a link without having spoken in chat first, spam protection active.\n");
								std::string temp = "/timeout ";
								temp.append (user.c_str());
								temp.append (" 60");
								send_room (room, temp);
								send_room (room, "My master doesn't like spambots, he says spambots are bad.");
							}
							else
							{
								// Check to see if SkidBot was directly addressed
								if (boost::iequals(chat.substr(0, 9), "SkidBot, "))
								{
									// Split the message apart
									std::string chat_remainder = chat.substr(9);
									std::vector<std::string> words;
									std::istringstream iss(chat_remainder);
									
									for (std::string token; std::getline(iss, token, ' ');)
									{
										words.push_back(std::move(token));
									}
									
									// If there are other words, try and work out the requested command
									if (words.size() > 0)
									{
										// Check any for any fixed commands
										if ((user.compare("n_skid11") == 0) && (boost::iequals(words[0], "respond")))
										{
											logger->log (": Responding to my master. :)\n");
											send_room (room, "Yes Master? :)");
										}
										else if ((user.compare("n_skid11") == 0) && (boost::iequals(chat_remainder, "please leave")))
										{
											logger->log (": Leaving by my masters request. :(\n");
											send_room (room, "OK, I'm going now, bye bye. :(");
											send_command ("PART", room);
										}
										else if ((user.compare("n_skid11") == 0) && (boost::iequals(words[0], "panic")))
										{
											logger->log (": Something has gone wrong, sending SIGTERM to my own process. :S\n");
											send_room (room, "Something has gone wrong, sending SIGTERM to my own process. panicBasket");
											raise (SIGTERM);
										}
										else if (boost::iequals(chat_remainder, "PC Specs"))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving my masters PC Specs to %s. :)\n", user.c_str());
												send_room (room, "You can find my masters PC specs on his You Tube channels about page, found here: http://www.youtube.com/c/SkidIncGaming/about :)");
												anti_spam = current_time;
											}
										}
										else if ((boost::iequals(words[0], "YouTube")) || (boost::iequals(chat_remainder, "You Tube")))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving my masters You Tube channel to %s. :)\n", user.c_str());
												send_room (room, "You can find my masters You Tube channel here: http://www.youtube.com/c/SkidIncGaming :)");
												anti_spam = current_time;
											}
										}
										else if (boost::iequals(chat, "Twitter"))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving my masters twitter username to %s. :)\n", user.c_str());
												send_room (room, "You can find my masters Twitter here: http://twitter.com/nskid11 :)");
												anti_spam = current_time;
											}
										}
										else if ((boost::iequals(words[0], "surround")) || (boost::iequals(words[0], "eyefinity")) || (boost::iequals(words[0], "multi-monitor")) || (boost::iequals(words[0], "resolution")))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving information on multi-monitor stream to %s. :)\n", user.c_str());
												send_room (room, "My masters is streaming at a triple-monitor resolution, twitch's layout isn't so great for this, so my master made this one that should display the stream better: http://www.skid-inc.net/eyestream.php :)");
												anti_spam = current_time;
											}
										}
										else if ((boost::iequals(words[0], "music")) || (boost::iequals(words[0], "song")))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving information on the music being played to %s. :)\n", user.c_str());
												send_room (room, "The music my master is playing will ether be from OC Remix, http://ocremix.org/, Rainwave, http://ocr.rainwave.cc/, or Miracle of Sound, http://miracleofsound.bandcamp.com/ :)");
												anti_spam = current_time;
											}
										}
										else if ((boost::iequals(words[0], "rules")) || (boost::iequals(chat_remainder, "channel rules")))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving the channels rules to %s. :)\n", user.c_str());
												send_room (room, "The rules for my masters channels are as follows, [1] Always be respectful to other people. [2] Seriously I'll instantly ban you if your not respectful to EVERYONE. [3] Please avoid spoilers. [4] I like to work things out myself, so if I miss something or don't say \"Hey, Chat, what does....\" then please don't tell me. [5] Don't spam. [6] I reserve the right to ban you for any other reason not covered here, just so you know :P");
												anti_spam = current_time;
											}
										}
										else if ((boost::iequals(words[0], "bsg")) || (boost::iequals(chat_remainder, "back seat gaming")) || (boost::iequals(chat_remainder, "back seat gamer")))
										{
											if ((current_time - anti_spam) > std::chrono::seconds(10))
											{
												logger->logf (": Giving back seat gaming information to %s. :)\n", user.c_str());
												send_room (room, "Please don't back seat game my master, he likes to play games how he likes to, regardless if that is optimal or not, he also likes to learn or work things out himself. So telling him what to do, or how to play, where things are, etc, will likely get you ignored or timed out or at worse banned. The exception to this rule is if he asks something directly of chat like, \"Chat, do you know how unlock this item?\". :)");
												anti_spam = current_time;
											}
										}
										
										
										// Fixed commands for rocksmith
										if (boost::iequals(current_game, "Rocksmith 2014"))
										{
											// Lets the users request my masters track list
											if ((boost::iequals(words[0], "tracks")) || (boost::iequals(chat_remainder, "track list")) || (boost::iequals(words[0], "Rocksmith")))
											{
												if ((current_time - anti_spam) > std::chrono::seconds(10))
												{
													logger->logf (": Giving link to my masters Rocksmith track list to %s. :)\n", user.c_str());
													send_room (room, "A full list of my masters Rocksmith songs can be found here, bear in mind favorated songs are first. http://www.skid-inc.net/rocksmith_tracks.php :)");
													anti_spam = current_time;
												}
											}
										}
										
										// Spoiler note
										if ((user.compare("n_skid11") == 0) && (boost::iequals(chat_remainder, "no spoilers start")))
										{
											logger->logf (": Starting to post no spoiler messages. :)\n");
											send_room (room, "Acknowledged, starting to post no spoiler messages every 5 minutes. :)");
											no_spoilers_running = true;
										}
										if ((user.compare("n_skid11") == 0) && (boost::iequals(chat_remainder, "no spoilers stop")))
										{
											logger->logf (": I will no longer post no spoiler messages. :)\n");
											send_room (room, "Acknowledged, I will no longer post no spoiler messages. :)");
											no_spoilers_running = false;
										}
									}
								}
								else if ((user.compare("n_skid11") == 0) && (boost::iequals(chat, "Good SkidBot")))
								{
									logger->log (": My master praised me ^_^.\n");
									send_room (room, "^_^");
								}
								
								// Commands used when streaming rocksmith
								// Track list
								// Requests enable / on
								// Requests disable / off
								// Requests add
								// Requests pop
								// Requests view
								// Requests clear
								
								// If the user hasn't chatted before, add them to the list
								if (!user_chatted)
								{
									logger->logf (": %s posted their first message without a link, adding them to the list.\n", user.c_str());
									users_chatted.push_back (user);
									std::sort (users_chatted.begin(), users_chatted.end());
								}
							}
						}
					}
				}
				else
				{
					// Checks for a ping message
					if ((message.length() > 4) && (message.substr(0, 4).compare("PING") == 0))
					{
						logger->debug (DEBUG_STANDARD, ": Playing ping pong with the servers.\n");
						send_command ("PONG", message.substr(5));
					}
					
					// Check for user mode change message	// :jtv MODE #n_skid11 +o paulscelus
					else if ((message.length() > 9) && (message.substr(5, 4).compare("MODE") == 0))
					{
						logger->debug (DEBUG_MINIMAL, ": I've found a MODE change for user.\n");
					}
					
					// Check for user list message			// :skidbot.tmi.twitch.tv 353 skidbot = #n_skid11 :arceusthepokemon wolf7th martinferrer ixtapa_ verenthes greenplane htbrdd ebula_viruss turkz813 jachunter guntherdw conjur0 dcirusc30 poewinter gone_nutty ptx3 loganfxcrafter strayparaSkidBot: I received: t = #zeekdageek :skidbot
					else if ((message.length() > 37) && (message.substr(0, 37).compare(":skidbot.tmi.twitch.tv 353 skidbot = ") == 0))
					{
						logger->logf (": I've found the channels NAMES list.\n");
						
						data_location = message.find (":", cmd_location);
						if (data_location != std::string::npos)
						{
							// Get the room the message was in
							room = message.substr(cmd_location + 8, data_location - cmd_location - 9);
							chat = message.substr(data_location + 1);
						}
					}
					
					// Check for join and part messages
					else
					{
						// Check for user join message			// :n_skid11!n_skid11@n_skid11.tmi.twitch.tv JOIN #n_skid11
						cmd_location = message.find ("JOIN");
						if (cmd_location != std::string::npos)
						{
							// Try to get the user
							user_location = message.find ("!");
							if (user_location != std::string::npos)
							{
								user = message.substr(1, user_location - 1);
							}
							else
							{
								user = "Unknown";
							}

							logger->logf (": I've noticed a user join the chat, %s.\n", user.c_str());
						}
						else
						{
							// Check for user part message			// :n_skid11!n_skid11@n_skid11.tmi.twitch.tv PART #n_skid11
							cmd_location = message.find ("PART");
							if (cmd_location != std::string::npos)
							{
								// Try to get the user
								user_location = message.find ("!");
								if (user_location != std::string::npos)
								{
									user = message.substr(1, user_location - 1);
								}
								else
								{
									user = "Unknown";
								}

								logger->logf (": I've noticed a user part the chat, %s.\n", user.c_str());
							}
						}
					}
				}
			}
			
			
			// Handles any messages in the groups queue
			while (girc_recv_buffer.size() > 0)
			{
				std::string message;
				std::string chat;
				std::string user;
				std::string room;
				size_t cmd_location;
				//size_t user_location;
				//size_t data_location;

				current_time = hrc_now;
				message = girc_recv_buffer.front();
				girc_recv_buffer.pop_front();

				// Otherwise looks for chat messages
				cmd_location = message.find ("PRIVMSG");
				if (cmd_location != std::string::npos)
				{
				}
				else
				{
					// Checks for a ping message
					if ((message.length() > 4) && (message.substr(0, 4).compare("PING") == 0))
					{
						logger->debug (DEBUG_STANDARD, ": Playing ping pong with the groups servers.\n");
						gsend_command ("PONG", message.substr(5));
					}
				}
			}
			
			
			// TODO: WORK OUT A BETTER WAY OF DOING THIS WITHOUT HARD CODING THE ROOM
			if (no_spoilers_running)
			{
				if ((current_time - no_spoilers) > std::chrono::minutes(5))
				{
					logger->log (": Posting no spoilers message.\n");
					send_room ("#n_skid11", "My master would like to do his first run blind, so please no spoilers or hints etc, thank you :)");
					no_spoilers = current_time;
				}
			}
		}

		usleep (1000);
	}

	// Find out why we are closing
	switch (close_reason)
	{
		case SIGTERM:
		case SIGQUIT:
		case SIGINT:
		{
			logger->log (": I received a signal to close the program, so I'm powering down.\n");
		}
		break;
		case SIGILL:
		{
			logger->log (": One of my threads performed an illegal instruction, my executable might be corrupt :(.\n");
		}
		break;
		case SIGSEGV:
		{
			logger->log (": One of my threads attempted to read outside of allocated memory, I'm leaking memory please help me.\n");
		}
		break;
		case SIGBUS:
		{
			logger->log (": One of my threads derefernced an invalid pointer, I may have uninitalized variable or of have been told to use of a null pointer.\n");
		}
		break;
	}
	
	// Close all the other threads
	irc_running = false;
	logger->log (": I'm waiting for the irc thread to end.\n");
	pthread_join (irc_thread, NULL);
	logger->log (": I'm waiting for the groups irc thread to end.\n");
	pthread_join (girc_thread, NULL);
	
	lock (tapi_mutex);
	tapi_running = false;
	release (tapi_mutex);
	logger->log (": I'm waiting for the Twitch api thread to end.\n");
	pthread_join (tapi_thread, NULL);
	
	// Clear any vectors or dynamic arrays
	users_chatted.clear ();
	
	logger->log (": I have closed.\n");

	delete mysql;
	delete logger;

	return 0;
}

// Hangles the SIGTERM signal, to safely close the program down
void signalHandler (int signum)
{
	switch (signum)
	{
		case SIGTERM:
		case SIGQUIT:
		case SIGINT:
		case SIGILL:
		case SIGSEGV:
		case SIGBUS:
		{
			// Stops the process from trying to close twice
			if (!closing_process)
			{
				close_reason = signum;
				closing_process = 1;
			}
		}
		break;
	}
}

// Spam catcher /[hH][tT][tT][pP]:\/\/[wW]{0,3}\.{0,1}.{1,5}\..{2,3}/
// Catch http://www. urls
/*if (boost::regex_search (chat.c_str(), boost::regex("[hH][tT][tT][pP]:\\/\\/[wW]{3}\\.")))
{
	if (boost::regex_search (chat.c_str(), boost::regex("[hH][tT][tT][pP]:\\/\\/[wW]{3}\\..{1,5}\\..{2,3}")))
	{
		logger->logf (": I've found spam, maybe?.\n");
		std::string temp = "/timeout ";
		temp.append (user.c_str());
		temp.append (" 60");
		send_room (room, temp);
		send_room (room, "My master doesn't like spambots, he says spambots are bad.");
	}
}
// Catch http:// urls
else if (boost::regex_search (chat.c_str(), boost::regex("[hH][tT][tT][pP]:\\/\\/.{1,5}\\..{2,3}")))
{
	logger->logf (": I've found spam, maybe?.\n");
	std::string temp = "/timeout ";
	temp.append (user.c_str());
	temp.append (" 60");
	send_room (room, temp);
	send_room (room, "My master doesn't like spambots, he says spambots are bad.");
}
// Catch www. urls
else if (boost::regex_search (chat.c_str(), boost::regex("[wW]{3}\\..{1,5}\\..{2,3}")))
{
	logger->logf (": I've found spam, maybe?.\n");
	std::string temp = "/timeout ";
	temp.append (user.c_str());
	temp.append (" 60");
	send_room (room, temp);
	send_room (room, "My master doesn't like spambots, he says spambots are bad.");
}*/

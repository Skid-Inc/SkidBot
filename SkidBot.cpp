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
#include <random>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include "SkidBot.hpp"
#include "Logger.hpp"
#include "MySQLHandler.hpp"
#include "IRCThread.hpp"
#include "TwitchAPIThread.hpp"

#define VERSION "0.31"

// Local function prototypes
std::string trim (std::string _str);
std::string parseDouble (double _value);
double rollQuerySplitSubAdd (std::string _query, std::string *_roll_text);
double rollQuerySplitMulDiv (std::string _query, std::string *_roll_text);
double rollQueryParse (std::string _query, std::string *_roll_text);
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

// Random variables
std::random_device dice;
std::string game_master = "n_skid11";

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
	//logger->log (": I'm starting my Twitch API thread so I can monitor the channel.\n");
	//pthread_create (&tapi_thread, NULL, TwitchAPIThread, NULL);
	//sleep (1);

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

							if ((!user_chatted) && (boost::regex_search (chat.c_str(), boost::regex("[^\\s.]\\.[^\\s.]{2,}"))))
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

							if ((!user_chatted) && (boost::regex_search (chat.c_str(), boost::regex("[^\\s.]\\.[^\\s.]{2,}"))))
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
									std::string chat_remainder = chat.substr (9);
									std::vector<std::string> words;
									std::istringstream iss (chat_remainder);

									for (std::string token; std::getline(iss, token, ' ');)
									{
										words.push_back (std::move(token));
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
												send_room (room, "The rules for my masters channels are as follows, [1] Always be respectful to other people. [2] Be respectful to other peoples opinions, just because someone else's opinion doesn't match your own, does not invalidate ether. [3] Please avoid spoilers. [4] I like to work things out myself, so if I miss something or don't say \"Hey, Chat, what does....\" then please don't tell me. [5] Don't spam, this includes emote spam.");
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

										// Change the game master
										if ((user.compare("n_skid11") == 0) && ((boost::iequals(words[0], "change")) || (boost::iequals(words[0], "set"))) && ((boost::iequals(words[1], "gm")) || (boost::iequals(words[1], "dm"))))
										{
											uint8_t target_word = 2;
											if (boost::iequals(words[2], "to"))
											{
												target_word = 3;
											}
											logger->logf (": I will change the assigned game master to %s.\n", words[target_word].c_str());
											game_master = words[target_word];
											std::string message = "Acknowledged, I will change the assigned game master to ";
											message += words[target_word];
											message += ". :)";
											send_room (room, message);
										}
										else if ((user.compare("n_skid11") == 0) && (boost::iequals(words[0], "who")) && ((boost::iequals(words.back(), "gm")) || (boost::iequals(words.back(), "dm"))))
										{
											logger->logf (": Reporting that the current game master is %s.\n", game_master.c_str());
											std::string message = "The currently assigned game master is ";
											message += game_master;
											message += ". :)";
											send_room (room, message);
										}
									}
								}
								else if ((user.compare("n_skid11") == 0) && (boost::iequals(chat, "Good SkidBot")))
								{
									logger->log (": My master praised me ^_^.\n");
									send_room (room, "^_^");
								}

								// Check to see if this is a dice roll
								else if ((boost::iequals(chat.substr(0, 6), "!roll ")) || (boost::iequals(chat.substr(0, 3), "!r ")) || (boost::iequals(chat.substr(0, 8), "!gmroll ")) || (boost::iequals(chat.substr(0, 5), "!gmr ")))
								{
									// Pull the roll query and set if this is a gm roll or not
									std::string roll_query;
									bool is_gm_roll = false;
									if (boost::iequals(chat.substr(0, 6), "!roll "))
									{
										roll_query = chat.substr(6);
										logger->debugf (DEBUG_MINIMAL, ": Someone is rolling %s\n", roll_query.c_str());
									}
									else if (boost::iequals(chat.substr(0, 3), "!r "))
									{
										roll_query = chat.substr(3);
										logger->debugf (DEBUG_MINIMAL, ": Someone is rolling %s\n", roll_query.c_str());
									}
									else if (boost::iequals(chat.substr(0, 8), "!gmroll "))
									{
										roll_query = chat.substr(8);
										is_gm_roll = true;
										logger->debugf (DEBUG_MINIMAL, ": Someone is gm rolling %s\n", roll_query.c_str());
									}
									else if (boost::iequals(chat.substr(0, 5), "!gmr "))
									{
										roll_query = chat.substr(5);
										is_gm_roll = true;
										logger->debugf (DEBUG_MINIMAL, ": Someone is gm rolling %s\n", roll_query.c_str());
									}

									// Prepare the variables used to process the roll
									std::string roll_text;
									double roll_result;
									std::string roll_reason = "some dice";

									// See if there is a reason for the roll
									std::size_t last_add = roll_query.find_last_of ('+');
									std::size_t last_sub = roll_query.find_last_of ('-');
									std::size_t last_mul = roll_query.find_last_of ('*');
									std::size_t last_div = roll_query.find_last_of ('/');
									std::size_t first_space = roll_query.find (' ');
									if ((last_add == std::string::npos) && (last_sub == std::string::npos) && (last_mul == std::string::npos) && (last_div == std::string::npos))
									{
										if (first_space != std::string::npos)
										{
											roll_reason = roll_query.substr (first_space + 1);
											roll_query = roll_query.substr (0, roll_query.length() - roll_reason.length() - 1);
										}
									}
									else
									{
										std::size_t highest_position = 0;
										if ((last_add != std::string::npos) && (last_add > highest_position))
										{
											highest_position = last_add;
										}
										if ((last_sub != std::string::npos) && (last_sub > highest_position))
										{
											highest_position = last_sub;
										}
										if ((last_mul != std::string::npos) && (last_mul > highest_position))
										{
											highest_position = last_mul;
										}
										if ((last_div != std::string::npos) && (last_div > highest_position))
										{
											highest_position = last_div;
										}

										// Find the first space after the last opperator
										first_space = roll_query.find (' ', highest_position + 2);
										if (first_space != std::string::npos)
										{
											roll_reason = roll_query.substr (first_space + 1);
											roll_query = roll_query.substr (0, roll_query.length() - roll_reason.length() - 1);
										}
									}

									// Parse the query
									roll_result = rollQuerySplitSubAdd (roll_query, &roll_text);


									// Send the results of the roll
									if (!is_gm_roll)
									{
										std::string temp;
										temp += user;
										temp += " just rolled ";
										temp += roll_reason;
										temp += ": ";
										temp += roll_text;
										temp += " = ";
										temp += parseDouble(roll_result);
										send_room (room, temp);
									}
									else
									{
										std::string temp = "/w ";
										temp += game_master;
										temp += " Game Master, ";
										temp += user;
										temp += " just rolled ";
										temp += roll_reason;
										temp += ": ";
										temp += roll_text;
										temp += " = ";
										gsend_room ("#jtv", temp);
									}
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

	//lock (tapi_mutex);
	//tapi_running = false;
	//release (tapi_mutex);
	//logger->log (": I'm waiting for the Twitch api thread to end.\n");
	//pthread_join (tapi_thread, NULL);

	// Clear any vectors or dynamic arrays
	users_chatted.clear ();

	logger->log (": I have closed.\n");

	delete mysql;
	delete logger;

	return 0;
}

// Strips whitespace from the begining and end of the string
std::string trim (std::string _str)
{
	std::size_t first = _str.find_first_not_of (' ');
	std::size_t last = _str.find_last_not_of (' ');
	return _str.substr (first, (last - first + 1));
}

// Converts a double into a string and removes trailing 0s
std::string parseDouble (double _value)
{
	char buffer[100];
	memset (buffer, 0, 100);
	snprintf (buffer, 100, "%g", _value);
	std::string result = buffer;
	return result;
}

// TODO: Add order of opperations, brackes
// TODO: Add !p penetrating exploding dice, after each exploded roll, sub 1 for that roll, IE (6, 6, 5 becomes, 6, 6-1, 5-2)
// TODO: Add rerolling dice, r<2 (rerolls 1s or 2s) r2r4 (rerolls 2s or 4s)
// TODO: FATE Dice??!!
// TODO: Add basic math functions (tie in with order of opperations, brackes
// TODO: Group rolls ({4d6+3d8}hk1) will keep the highest of all dice, ({4d6},{3d8}hk1) will keep the highest group

// Splits apart a roll query on a minus sign, then processes the result
double rollQuerySplitSubAdd (std::string _query, std::string *_roll_text)
{
	// Split the message apart
	std::string text_return;
	std::vector<std::string> sub_add_parts;
	std::vector<char> sub_add_ops;
	std::string part;
	uint32_t t;
	std::size_t last_pos = 0;
	for (t = 0; t < _query.length(); t++)
	{
		char c = _query.at(t);
		// Check characture for -
		if (c == '-')
		{
			// Split the query at opperator and add it vector
			part = _query.substr(last_pos, t - last_pos);
			sub_add_parts.push_back (trim(part));
			sub_add_ops.push_back ('-');
			last_pos = t + 1;
		}
		// Else check characture for +
		else if (c == '+')
		{
			// Split the query at opperator and add it vector
			part = _query.substr(last_pos, t - last_pos);
			sub_add_parts.push_back (trim(part));
			sub_add_ops.push_back ('+');
			last_pos = t + 1;
		}
	}
	// Add the last part of the query to vector
	part = _query.substr(last_pos, t - last_pos);
	sub_add_parts.push_back (trim(part));

	// If there is just one element send it on
	if (sub_add_parts.size() == 1)
	{
		logger->debugf (DEBUG_DETAILED, ": SA Skip %s\n", sub_add_parts[0].c_str());
		return rollQuerySplitMulDiv (sub_add_parts[0], &*_roll_text);
	}
	else if (sub_add_parts.size() > 1)
	{
		// Parse the first value
		logger->debugf (DEBUG_DETAILED, ": SA Start %s\n", sub_add_parts[0].c_str());
		double temp_value = rollQuerySplitMulDiv (sub_add_parts[0], &text_return);
		*_roll_text = text_return;

		// Parse every other value and subtract or add them
		uint8_t sub_add_t;
		for (sub_add_t = 1; sub_add_t < sub_add_parts.size(); sub_add_t++)
		{
			if (sub_add_ops[sub_add_t-1] == '-')
			{
				logger->debugf (DEBUG_DETAILED, ": Sub %s\n", sub_add_parts[sub_add_t].c_str());
				temp_value -= rollQuerySplitMulDiv (sub_add_parts[sub_add_t], &text_return);
				*_roll_text += " -";
				*_roll_text += text_return;
			}
			else
			{
				logger->debugf (DEBUG_DETAILED, ": Add %s\n", sub_add_parts[sub_add_t].c_str());
				temp_value += rollQuerySplitMulDiv (sub_add_parts[sub_add_t], &text_return);
				*_roll_text += " +";
				*_roll_text += text_return;
			}
		}
		return temp_value;
	}

	return 0;
}

// Splits apart a roll query on a multiply sign, then processes the result
double rollQuerySplitMulDiv (std::string _query, std::string *_roll_text)
{
	// Split the message apart
	std::string text_return;
	std::vector<std::string> mul_div_parts;
	std::vector<char> mul_div_ops;
	std::string part;
	uint32_t t;
	std::size_t last_pos = 0;
	for (t = 0; t < _query.length(); t++)
	{
		char c = _query.at(t);
		// Check characture for -
		if (c == '*')
		{
			// Split the query at opperator and add it vector
			part = _query.substr(last_pos, t - last_pos);
			mul_div_parts.push_back (trim(part));
			mul_div_ops.push_back ('*');
			last_pos = t + 1;
		}
		// Else check characture for +
		else if (c == '/')
		{
			// Split the query at opperator and add it vector
			part = _query.substr(last_pos, t - last_pos);
			mul_div_parts.push_back (trim(part));
			mul_div_ops.push_back ('/');
			last_pos = t + 1;
		}
	}
	// Add the last part of the query to vector
	part = _query.substr(last_pos, t - last_pos);
	mul_div_parts.push_back (trim(part));

	// If there is just one element send it on
	if (mul_div_parts.size() == 1)
	{
		logger->debugf (DEBUG_DETAILED, ": MD Skip %s\n", mul_div_parts[0].c_str());
		double result = rollQueryParse (mul_div_parts[0], &text_return);
		*_roll_text = text_return;
		return result;
	}
	else if (mul_div_parts.size() > 1)
	{
		// Parse the first value
		logger->debugf (DEBUG_DETAILED, ": MD Start %s\n", mul_div_parts[0].c_str());
		double temp_value = rollQueryParse (mul_div_parts[0], &text_return);
		*_roll_text = text_return;

		// Parse every other value and multiply or divid them
		uint8_t mul_div_t;
		for (mul_div_t = 1; mul_div_t < mul_div_parts.size(); mul_div_t++)
		{
			if (mul_div_ops[mul_div_t-1] == '*')
			{
				logger->debugf (DEBUG_DETAILED, ": Mul %s\n", mul_div_parts[mul_div_t].c_str());
				temp_value *= rollQueryParse (mul_div_parts[mul_div_t], &text_return);
				*_roll_text += " *";
				*_roll_text += text_return;
			}
			else
			{
				logger->debugf (DEBUG_DETAILED, ": Div %s\n", mul_div_parts[mul_div_t].c_str());
				double parse_return = rollQueryParse (mul_div_parts[mul_div_t], &text_return);
				if (parse_return != 0)
				{
					temp_value /= parse_return;
					*_roll_text += " /";
					*_roll_text += text_return;
				}
				else
				{
					*_roll_text = "Divid by zero";
					return 0;
				}
			}
		}
		return temp_value;
	}

	return 0;
}

double rollQueryParse (std::string _query, std::string *_roll_text)
{
	logger->debugf (DEBUG_DETAILED, ": Parse %s\n", _query.c_str());
	// Variables used in dice rolling
	*_roll_text = "";
	double result = 0;
	uint8_t dice_limit = 20;
	uint32_t num_of_dice = 1;
	uint32_t type_of_dice = 6;
	uint8_t discard_high = 0;
	uint8_t discard_low = 0;
	bool explode = false;
	bool compound = false;

	// Variables used to parse the roll
	uint32_t t;
	char field[10];
	memset (field, 0, 32);
	uint8_t field_count = 0;
	bool found_dice = false;
	bool found_type = false;
	bool found_dl = false;
	bool found_dh = false;
	bool found_kl = false;
	bool found_kh = false;
	bool found_expl = false;
	char c;

	// Loop query
	for (t = 0; t < _query.length(); t++)
	{
		c = _query.at(t);
		field[field_count++] = c;

		// We must wait until we've found the number of dice
		if (!found_dice)
		{
			// Look for dice characture
			if ((c == 'd') || (c == 'D'))
			{
				field[field_count] = 0;
				field_count = 0;
				// If this isn't the first characture, then set the number of dices, and check again limit
				if (t != 0)
				{
					num_of_dice = strtol (field, NULL, 10);
					if (num_of_dice > dice_limit)
					{
						num_of_dice = dice_limit;
					}
				}
				found_dice = true;
			}
		}
		// If we are waiting for the number of low dice to drop
		else if (found_dl)
		{
			if ((!isdigit(c)) || (t == _query.length()-1))
			{
				field[field_count] = 0;
				field_count = 0;
				discard_low = strtol (field, NULL, 10);
				found_dl = false;
				t--;
			}
		}
		// If we are waiting for the number of high dice to drop
		else if (found_dh)
		{
			if ((!isdigit(c)) || (t == _query.length()-1))
			{
				field[field_count] = 0;
				field_count = 0;
				discard_high = strtol (field, NULL, 10);
				found_dh = false;
				t--;
			}
		}
		// If we are waiting for the number of  low dice to keep
		else if (found_kl)
		{
			if ((!isdigit(c)) || (t == _query.length()-1))
			{
				field[field_count] = 0;
				field_count = 0;
				discard_high = num_of_dice - strtol (field, NULL, 10);
				found_kl = false;
				t--;
			}
		}
		// If we are waiting for the number of  high dice to keep
		else if (found_kh)
		{
			if ((!isdigit(c)) || (t == _query.length()-1))
			{
				field[field_count] = 0;
				field_count = 0;
				discard_low = num_of_dice - strtol (field, NULL, 10);
				found_kh = false;
				t--;
			}
		}
		else if (found_expl)
		{
			if ((c == '!') || (t == _query.length()-1))
			{
				compound = true;
			}
			found_expl = false;
			t--;
		}
		else
		{
			// Look for drop characture
			if ((c == 'd') || (c == 'D'))
			{
				if (!found_type)
				{
					field[field_count] = 0;
					type_of_dice = strtol (field, NULL, 10);
					found_type = true;
				}
				field_count = 0;

				// Check if we are discarding highest or lower or if it's not defined
				c = _query.at(t+1);
				if ((c == 'h') || (c == 'H'))
				{
					found_dh = true;
					t++;
				}
				else if ((c == 'l') || (c == 'L'))
				{
					found_dl = true;
					t++;
				}
				else
				{
					found_dl = true;
				}
			}
			// Look for keep characture
			else if ((c == 'k') || (c == 'K'))
			{
				if (!found_type)
				{
					field[field_count] = 0;
					field_count = 0;
					type_of_dice = strtol (field, NULL, 10);
					found_type = true;
				}
				field_count = 0;

				// Check if we are keeping highest or lower or if it's not defined
				c = _query.at(t+1);
				if ((c == 'h') || (c == 'H'))
				{
					found_kh = true;
					t++;
				}
				else if ((c == 'l') || (c == 'L'))
				{
					found_kl = true;
					t++;
				}
				else
				{
					found_kh = true;
				}
			}
			// Look for explode characture
			else if (c == '!')
			{
				if (!found_type)
				{
					field[field_count] = 0;
					type_of_dice = strtol (field, NULL, 10);
					found_type = true;
				}
				field_count = 0;
				explode = true;
				found_expl = true;
			}
		}

		// If we reach here and field_count is still 10, the query is bad
		if (field_count >= 10)
		{
			*_roll_text = "Query field too long";
			return 0;
		}
	}

	// If we've reached here and haven't found the number of dice, then this is just a number
	if (!found_dice)
	{
		result = strtod (_query.c_str(), NULL);
		*_roll_text = " ";
		*_roll_text += parseDouble(result);
		return result;
	}

	// If we finish the query without finding the type of dice, the remainder should be the type of dice
	if (!found_type)
	{
		field[field_count] = 0;
		type_of_dice = strtol (field, NULL, 10);
	}

	// Debug
	logger->debugf (DEBUG_DETAILED, ": DH, %d, DL, %d, Explode, %d, Compound, %d\n", discard_high, discard_low, explode, compound);

	// Roll them bones
	uint8_t bone;
	std::vector<roll_data> rolls;
	std::uniform_int_distribution<int> distribution(1, type_of_dice);
	for (bone = 0; bone < num_of_dice; bone++)
	{
		// Create the data and roll the bone
		uint32_t roll = distribution (dice);
		roll_data data;

		// If this dice should explode, then explode
		if (explode)
		{
			// If we roll max, keep rolling until we done
			while (roll == type_of_dice)
			{
				data.exploded = true;
				data.roll += roll;
				if (!compound)
				{
					data.text += std::to_string (roll);
					data.text += "] [";
				}
				roll = distribution (dice);
			}

			// Dice is done exploding, finish calculating the total
			data.roll += roll;
			if (compound)
			{
				data.text += std::to_string (data.roll);
			}
			else
			{
				data.text += std::to_string (roll);
			}
			data.text += "]";
		}
		else
		{
			data.roll += roll;
			data.text += std::to_string (roll);
			data.text += "]";
		}
		rolls.push_back (data);
	}

	// Discard the lowest bones
	uint8_t discard;
	for (discard = 0; (discard < discard_low) && (discard < num_of_dice); discard++)
	{
		uint8_t dice_to_discard = 0;
		uint32_t lowest_value = type_of_dice + 1;
		for (t = 0; t < num_of_dice; t++)
		{
			if (rolls[t].roll < lowest_value)
			{
				if (!rolls[t].discarded)
				{
					dice_to_discard = t;
					lowest_value = rolls[t].roll;
				}
			}
		}
		rolls[dice_to_discard].discarded = true;
	}
	// Discard the highest bones
	for (discard = 0; (discard < discard_high) && (discard < num_of_dice); discard++)
	{
		uint8_t dice_to_discard = 0;
		uint32_t highest_value = 0;
		for (t = 0; t < num_of_dice; t++)
		{
			if (rolls[t].roll > highest_value)
			{
				if (!rolls[t].discarded)
				{
					dice_to_discard = t;
					highest_value = rolls[t].roll;
				}
			}
		}
		rolls[dice_to_discard].discarded = true;
	}

	// Count up the bones
	for (roll_data data : rolls)
	{
		if (data.exploded)
		{
			*_roll_text += " {";
			if (data.discarded)
			{
				*_roll_text += "x";
			}
			else
			{
				result += data.roll;
			}
			*_roll_text += "[";
			*_roll_text += data.text;
			*_roll_text += "} ";
		}
		else
		{
			*_roll_text += " [";
			if (data.discarded)
			{
				*_roll_text += "x";
			}
			else
			{
				result += data.roll;
			}
			*_roll_text += data.text;
		}
	}

	return result;
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

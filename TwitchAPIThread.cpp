// TODO: Read the current follower and the follower before on load, so that if someone unfollowers it doesn't thank them again.
// NOTE: The above won't be nesseracy with a user database.
#include <unistd.h>

#include <string>
#include <curl/curl.h>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include "TwitchAPIThread.hpp"
#include "SkidBot.hpp"
#include "Logger.hpp"
#include "IRCThread.hpp"


// Global varibles
bool tapi_running = true;
pthread_mutex_t tapi_mutex = PTHREAD_MUTEX_INITIALIZER;

// API variables
std::string curl_buffer;
std::string last_follower = "";
std::string previous_follower = "";

// Variables that SkidBot needs to keep track of
std::chrono::high_resolution_clock::time_point last_channel_request;
std::string current_title = "Undefined";
std::string current_game = "Undefined";

extern Logger *logger;


/**
 * Curl callback function, copies the reply into a static string
 */
static size_t WriteCallback (void *contents, size_t size, size_t nmemb, void *userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}


/**
 * TwitchAPIThread, handles connecting to the Twitch api, sends requests and parses the results.
 */
void *TwitchAPIThread (void *)
{
	CURL *curl;
	CURLcode res;
	
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init ();
	
	lock (tapi_mutex);
	while (tapi_running)
	{
		release (tapi_mutex);
		
		// If curl is not valid re-init it
		if (!curl)
		{
			curl = curl_easy_init ();
		}
		
		// Request the most recent follower
		curl_buffer.clear();
		curl_easy_setopt (curl, CURLOPT_URL, "https://api.twitch.tv/kraken/channels/n_skid11/follows?limit=2");
		curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt (curl, CURLOPT_WRITEDATA, &curl_buffer);
		res = curl_easy_perform (curl);
		curl_easy_reset (curl);
		if (res != CURLE_OK)
		{
			logger->logf (" TwitchAPIThread: I was unable to send follower api request, reason: %s\n", curl_easy_strerror(res));
		}
		else
		{
			// Find the username and displayname for the last follower
			std::string latest_follower = "";
			std::string displayname = "";
			size_t start_username = curl_buffer.find ("\"name\":\"") + 8;
			size_t end_username = curl_buffer.find ("\",", start_username);
			size_t start_displayname = curl_buffer.find ("\"display_name\":\"") + 16;
			size_t end_displayname = curl_buffer.find ("\",", start_displayname);
			
			if ((start_username != std::string::npos) && (end_username != std::string::npos))
			{
				latest_follower = curl_buffer.substr (start_username, end_username - start_username);
			}
			if ((start_displayname != std::string::npos) && (end_displayname != std::string::npos))
			{
				displayname = curl_buffer.substr (start_displayname, end_displayname - start_displayname);
			}
			
			// If we found a valid username and displayname
			if ((latest_follower.length() != 0) && (displayname.length() != 0))
			{
				// If the last follower hasn't been read yet, don't send a PM
				if (last_follower.length() == 0)
				{
					last_follower = latest_follower;
					std::string temp = "/w n_skid11 Master, the last follower was ";
					temp.append (displayname);
					temp.append (".");
					gsend_room ("#jtv", temp);
					
					logger->logf (" TwitchAPIThread: I've found the last follower, %s.\n", displayname.c_str());
					
					// Get the follower before last
					start_username = curl_buffer.find ("\"name\":\"", end_displayname) + 8;
					end_username = curl_buffer.find ("\",", start_username);
					start_displayname = curl_buffer.find ("\"display_name\":\"", end_username) + 16;
					end_displayname = curl_buffer.find ("\",", start_displayname);
					if ((start_username != std::string::npos) && (end_username != std::string::npos))
					{
						previous_follower = curl_buffer.substr (start_username, end_username - start_username);
					}
					if ((start_displayname != std::string::npos) && (end_displayname != std::string::npos))
					{
						displayname = curl_buffer.substr (start_displayname, end_displayname - start_displayname);
					}
					
					logger->logf (" TwitchAPIThread: I've found the follower before last, %s.\n", displayname.c_str());
				}
				else if (previous_follower.compare(latest_follower) == 0)
				{
					// Get the follower before last
					start_username = curl_buffer.find ("\"name\":\"", end_displayname) + 8;
					end_username = curl_buffer.find ("\",", start_username);
					start_displayname = curl_buffer.find ("\"display_name\":\"", end_username) + 16;
					end_displayname = curl_buffer.find ("\",", start_displayname);
					if ((start_username != std::string::npos) && (end_username != std::string::npos))
					{
						previous_follower = curl_buffer.substr (start_username, end_username - start_username);
					}
					if ((start_displayname != std::string::npos) && (end_displayname != std::string::npos))
					{
						displayname = curl_buffer.substr (start_displayname, end_displayname - start_displayname);
					}
					
					logger->logf (" TwitchAPIThread: Master, the last person to follow unfollowed :(, %s.\n", displayname.c_str());
				}
				else if (last_follower.compare(latest_follower) != 0)
				{
					
					// Confirm the name is valid and that something hasn't messed up with the API call
					if ((!boost::regex_search (latest_follower.c_str(), boost::regex("[^a-zA-Z0-9_]"))) && ((!boost::regex_search (last_follower.c_str(), boost::regex("[^a-zA-Z0-9_]")))))
					{
						// Confirm the display name is valid and that something hasn't messed up with the API call
						if (!boost::regex_search (displayname.c_str(), boost::regex("[^a-zA-Z0-9_]")))
						{
							previous_follower = last_follower;
							last_follower = latest_follower;
							std::string temp = "/w ";
							temp.append (latest_follower);
							temp.append (" ");
							temp.append (displayname);
							temp.append (" my master would like me to thank you for following, while he doesn't announce such things on stream he does still appreciate it, so thank you. :)");
							gsend_room ("#jtv", temp);
							temp.clear ();
							temp.append ("/w n_skid11 Master, ");
							temp.append (displayname);
							temp.append (" just followed, thought you should know. :)");
							gsend_room ("#jtv", temp);

							logger->logf (" TwitchAPIThread: I've found a new follower, %s.\n", displayname.c_str());
						}
					}
				}
			}
		}
		
		// Every 1 minute check the stream title and game
		if ((hrc_now - last_channel_request) > std::chrono::seconds(60))
		{
			last_channel_request = hrc_now;
			
			curl_buffer.clear();
			curl_easy_setopt (curl, CURLOPT_URL, "https://api.twitch.tv/kraken/channels/n_skid11");
			curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, WriteCallback);
			curl_easy_setopt (curl, CURLOPT_WRITEDATA, &curl_buffer);
			res = curl_easy_perform (curl);
			curl_easy_reset (curl);
			if (res != CURLE_OK)
			{
				logger->logf (" TwitchAPIThread: I was unable to send channel api request, reason: %s\n", curl_easy_strerror(res));
			}
			else
			{
				size_t start_status = curl_buffer.find ("\"status\":\"") + 10;
				size_t end_status = curl_buffer.find ("\",", start_status);
				size_t start_game = curl_buffer.find ("\"game\":\"") + 8;
				size_t end_game = curl_buffer.find ("\",", start_game);
				
				if ((start_status != std::string::npos) && (end_status != std::string::npos))
				{
					current_title = curl_buffer.substr (start_status, end_status - start_status);
				}
				if ((start_game != std::string::npos) && (end_game != std::string::npos))
				{
					current_game = curl_buffer.substr (start_game, end_game - start_game);
				}
				
				logger->debugf (DEBUG_STANDARD, " TwitchAPIThread: I've found the stream title and game, %s, %s.\n", current_title.c_str(), current_game.c_str());
			}
		}
		
		usleep (1000000);
		
		lock (tapi_mutex);
	}
	release (tapi_mutex);
	
	curl_easy_cleanup (curl);
	curl_global_cleanup ();
	
	logger->log (" TwitchAPIThread: I've stopped the Twitch API thread.\n");
	
	return NULL;
}

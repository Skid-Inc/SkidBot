#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <pthread.h>
#include <sys/time.h>

#include "Logger.hpp"
#include "SkidBot.hpp"

/**
 * Creates a basic logger class using an unset log file name
 */
Logger::Logger ()
{
	log_file = NULL;
	log_file_name = "/var/log/Unset-Log-File.log";
	line_prefix = "LOGGER";
	debug_level = 0;
	last_log_line = "";
	last_log_count = 0;
	log_mutex = PTHREAD_MUTEX_INITIALIZER;

	// Report logger initialisation
	//logf (": Initialised with log file: %s.\n", log_file_name.c_str ());
}


/**
 * Creates a basic logger class using the given log file name
 */
Logger::Logger (const char *log_file_location)
{
	log_file = NULL;
	log_file_name = log_file_location;
	line_prefix = "LOGGER";
	debug_level = 0;
	last_log_line = "";
	last_log_count = 0;
	log_mutex = PTHREAD_MUTEX_INITIALIZER;

	// Report logger initialisation
	logf (": Initialised with log file: %s\n", log_file_name.c_str ());
}


/**
 * Forces the log file to close and destroys the logger
 */
Logger::~Logger ()
{
	log_file = NULL;
}


/**
 * Sets the log files location to the given file name
 */
void Logger::setLogFileLocation (const char *log_file_location)
{
	log_file_name = log_file_location;
	logf (" LOGGER: Initialised with log file: %s.\n", log_file_name.c_str ());
}


/**
 *
 */
void Logger::setLinePrefix (const char *new_line_prefix)
{
	line_prefix = new_line_prefix;
}


/**
 * Sets the debug level to the given level
 */
void Logger::setDebugLevel (uint8_t new_debug_level)
{
	debug_level = new_debug_level;
}


/**
 * Opens a file so the can log messages
 */
int Logger::initLogFile (void)
{
        log_file = fopen (log_file_name.c_str (), "a+");
        if (log_file == NULL)
        {
			printf ("%s: Logger unable to open log file.\n", line_prefix);
			return -1;
        }
        return 1;
}


/**
 * Writes the given line into the logfile as well as printing the line out to screen
 */
void Logger::log (const char *line)
{
	lock (log_mutex);

	printf("%s%s", line_prefix, line);
	
	// If this log message was only just sent
	if (last_log_line.compare (line) == 0)
	{
		last_log_count++;
	}
	else
	{
		if (initLogFile() == 1)
		{
			char time_buffer[21];

			memset(time_buffer, 0, 21);
			time_t time_of_day;
			time (&time_of_day);
			strftime (time_buffer, 21, "%Y/%m/%d %H:%M:%S ", gmtime(&time_of_day));
			
			// Store how many times the last message repeated
			last_log_line = line;
			if (last_log_count > 0)
			{
				char buffer[64];
				std::string log_repeat;
				
				snprintf (buffer, 64, "(Last message repeated %d times.)\n", last_log_count);
				log_repeat = buffer;
				
				fprintf (log_file, "%s%s", time_buffer, log_repeat.c_str ());
				
				last_log_count = 0;
			}

			fprintf (log_file, "%s%s%s", time_buffer, line_prefix, line);

			closeLogFile ();
		}
	}

	release (log_mutex);
}


/**
 * Uses the given format and agruments to build the line to send to printLog
 */
void Logger::logf (const char *format, ...)
{
	va_list args;
	va_start (args, format);

	char buffer[LOGGER_MAX_BUFFER];

	memset (buffer, 0, strlen (buffer));
	vsnprintf (buffer, LOGGER_MAX_BUFFER, format, args);
	log (buffer);

	va_end(args);
}


/**
 * Writes the given line into the logfile as well as printing the line out to screen
 */
void Logger::logx (unsigned char hex, bool end_line)
{
	lock (log_mutex);

	printf("%02x ", hex);
	if (end_line)
	{
		printf ("\n");
	}
	if (initLogFile() == 1)
	{
		fprintf (log_file, "%02x ", hex);
		if (end_line)
		{
			fprintf (log_file, "\n");
		}

		closeLogFile ();
	}

	release (log_mutex);
}


/**
 * Writes the given line into the logfile as well as printing the line out to screen
 */
void Logger::debug (uint8_t debug_level, const char *line)
{
	lock (log_mutex);

	if (debug_level <= this->debug_level)
	{
		printf("%s DEBUG %d%s", line_prefix, debug_level, line);

		// If this log message was only just sent
		if (last_log_line.compare (line) == 0)
		{
			last_log_count++;
		}
		else
		{
			if (initLogFile() == 1)
			{
				char time_buffer[21];

				memset(time_buffer, 0, 21);
				time_t time_of_day;
				time (&time_of_day);
				strftime (time_buffer, 21, "%Y/%m/%d %H:%M:%S ", gmtime(&time_of_day));

				// Store how many times the last message repeated
				last_log_line = line;
				if (last_log_count > 0)
				{
					char buffer[64];
					std::string log_repeat;

					snprintf (buffer, 64, "(Last message repeated %d times.)\n", last_log_count);
					log_repeat = buffer;

					fprintf (log_file, "%s%s", time_buffer, log_repeat.c_str ());

					last_log_count = 0;
				}

				fprintf (log_file, "%s%s DEBUG %d%s", time_buffer, line_prefix, debug_level, line);

				closeLogFile ();
			}
		}
	}

	release (log_mutex);
}


/**
 * Uses the given format and agruments to build the line to send to printLog
 */
void Logger::debugf (uint8_t debug_level, const char *format, ...)
{
	if (debug_level <= this->debug_level)
	{
		va_list args;
		va_start (args, format);

		char buffer[LOGGER_MAX_BUFFER];

		memset (buffer, 0, strlen (buffer));
		vsnprintf (buffer, LOGGER_MAX_BUFFER, format, args);
		debug (debug_level, buffer);

		va_end(args);
	}
}


/**
 * Writes the given line into the logfile as well as printing the line out to screen
 */
void Logger::debugx (uint8_t debug_level, unsigned char hex, bool end_line)
{
	lock (log_mutex);

	if (debug_level <= this->debug_level)
	{
		printf("%02x ", hex);
		if (end_line)
		{
			printf ("\n");
		}
		if (initLogFile() == 1)
		{
			fprintf (log_file, "%02x ", hex);
			if (end_line)
			{
				fprintf (log_file, "\n");
			}

			closeLogFile ();
		}
	}

	release (log_mutex);
}


/**
 * Closes the log file
 */
void Logger::closeLogFile (void)
{
	fclose (log_file);
}

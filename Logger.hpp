#ifndef	_LOGGER_H
#define _LOGGER_H

#include <string>

// Defines the max buffer size of the Logger
#define LOGGER_MAX_BUFFER	1024

// Defines the debug levels
#define DEBUG_NONE			0
#define DEBUG_MINIMAL		1
#define DEBUG_STANDARD		2
#define DEBUG_DETAILED		3

// Define the Logger class
class Logger;

// Build the Logger class Template
class Logger
{
private:
	// Private variables
	FILE *log_file;
	std::string log_file_name;
	const char *line_prefix;
	uint8_t debug_level;
	std::string last_log_line;
	uint32_t last_log_count;
	pthread_mutex_t log_mutex;

	// Private methods
	int initLogFile (void);
	void closeLogFile (void);

public:
	// Constructors and destructor
	Logger ();
	Logger (const char *log_file_location);
	~Logger ();

	// Public methods
	void setLogFileLocation (const char *log_file_location);
	void setLinePrefix (const char *new_line_prefix);
	void setDebugLevel (uint8_t new_debug_level);
	void logf (const char *format, ...);
	void log (const char *line);
	void logx (unsigned char hex, bool end_line);
	void debugf (uint8_t debug_level, const char *format, ...);
	void debug (uint8_t debug_level, const char *line);
	void debugx (uint8_t debug_level, unsigned char hex, bool end_line);
};

#endif

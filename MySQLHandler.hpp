#ifndef	_MYSQL_HANDLER_H
#define _MYSQL_HANDLER_H

#include <pthread.h>
#include <mysql.h>
#include "Logger.hpp"

// Global function prototypes
int mysqlConnect (void);
void mysqlDisconnect (void);
MYSQL_RES* mysqlQuery (const char *format, ...);

// Define the Process class
class MySQLHandler;

// Build the Process class template
class MySQLHandler
{
private:
	// Private variables
	MYSQL *connection = NULL;
	pthread_mutex_t mysql_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t query_mutex = PTHREAD_MUTEX_INITIALIZER;
	Logger *logger;
	char query[10240];

	// Private methods

public:
	// Constructors and destructor
	MySQLHandler (Logger *new_logger);
	~MySQLHandler ();

	// Public methods
	void setLogger (Logger *new_logger);
	int mysqlConnect (void);
	void mysqlDisconnect (void);
	MYSQL_RES* mysqlQuery (const char *format, ...);
	unsigned long long mysqlAffectedRows (void);

};

#endif

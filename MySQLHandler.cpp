#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <mysql/errmsg.h>
#include <mysql/mysql.h>

#include "MySQLHandler.hpp"
#include "SkidBot.hpp"
#include "Logger.hpp"


/**
 * Creates a basic MySQL object using the given logger
 */
MySQLHandler::MySQLHandler (Logger *new_logger)
{
	connection = NULL;
	mysql_mutex = PTHREAD_MUTEX_INITIALIZER;
	query_mutex = PTHREAD_MUTEX_INITIALIZER;
	logger = new_logger;

	logger->log (" MYSQL: Object created, attempting to connect.\n");
	mysqlConnect ();
}


/**
 * Disconnects the connection and unlinks the logger
 */
MySQLHandler::~MySQLHandler ()
{
	if (connection != NULL)
	{
		mysqlDisconnect ();
	}
	logger = NULL;
}


/**
 * Sets the logger this object will use
 */
void MySQLHandler::setLogger (Logger *new_logger)
{
	logger = new_logger;
}


/**
 * Connect to the MySQL database
 */
int MySQLHandler::mysqlConnect (void)
{
	logger->debug (DEBUG_STANDARD, " MYSQL DEBUG 2: mysqlConnect called.\n");

	lock (mysql_mutex);

	connection = mysql_init (NULL);
	if (!connection)
	{
		logger->log (" MYSQL: Failed to initialise MySQL connection.\n");
		release (mysql_mutex);
		return -1;
	}

	// Attempts to connect to the database
	connection = mysql_real_connect (connection, "localhost", "root", "SkidBot", "1234567890", 0, NULL, 0);
	if (connection)
	{
		logger->log (" MYSQL: MySQL connection successful.\n");
		release (mysql_mutex);
		return 1;
	}
	else
	{
		logger->log (" MYSQL: MySQL connection failed.\n");
		connection = NULL;
		release (mysql_mutex);
		return -2;
	}
}


/**
 * Disconnect from the MySQL database
 */
void MySQLHandler::mysqlDisconnect (void)
{
	logger->debug (DEBUG_STANDARD, " MYSQL DEBUG 2: mysqlDisconnect called.\n");

	lock (mysql_mutex);

	if (connection != NULL)
	{
		mysql_close (connection);
		connection = NULL;
	}

	release (mysql_mutex);
}


/**
 * Perform a query (returns a result set)
 */
MYSQL_RES* MySQLHandler::mysqlQuery (const char *format, ...)
{
	lock (query_mutex);

	// Check to make sure we have a connection and then perform the query
	if (connection)
	{
		// Builds the query
		va_list args;
		va_start (args, format);
		
		char query[512];
		memset (query, 0, strlen (query));
		vsnprintf (query, 512, format, args);
		
		MYSQL_RES* temp_result_set;

		logger->debugf (DEBUG_DETAILED, " MYSQL DEBUG 3: mysqlQuery called with, %s.\n", query);

		lock (mysql_mutex);
		if (mysql_query (connection, query))
		{
			logger->debugf (DEBUG_MINIMAL, " MYSQL DEBUG 1: Query failed, %s.\n", mysql_error (connection));

			// If the server has disconnected, reconnect and try again
			if ((mysql_errno (connection) == CR_SERVER_GONE_ERROR) || (mysql_errno (connection) == CR_SERVER_LOST))
			{
				release (mysql_mutex);
				mysqlDisconnect ();
				mysqlConnect ();
				lock (mysql_mutex);
				if (mysql_query (connection, query))
				{
					logger->debugf (DEBUG_MINIMAL, " MYSQL DEBUG 1: Query failed again after reconnect, %s.\n", mysql_error (connection));
					va_end(args);
					release (mysql_mutex);
					release (query_mutex);

					return NULL;
				}
			}
			else
			{
				va_end(args);
				release (mysql_mutex);
				release (query_mutex);

				return NULL;
			}
		}

		temp_result_set = mysql_store_result (connection);

		va_end(args);
		release (mysql_mutex);
		release (query_mutex);

		return temp_result_set;
	}
	else
	{
		release (query_mutex);
		logger->log (" MYSQL: Failed to perform the query because there was no connection.\n");

		return NULL;
	}
}

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <mysql/errmsg.h>
#include <mysql/mysql.h>
#include <unistd.h>

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

	logger->log (" MYSQL: Object created.\n");
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
 * Configures the MySQLHandler, and attempts to connect
 */
int MySQLHandler::init (std::string _db_user, std::string _db_pass, std::string _db_name)
{
	db_user = _db_user;
	db_pass = _db_pass;
	db_name = _db_name;

	logger->log (" MYSQL: Object initalised, attempting to connect.\n");

	return mysqlConnect ();
}


/**
 * Connect to the MySQL database
 */
int MySQLHandler::mysqlConnect (void)
{
	logger->debug (DEBUG_STANDARD, " MYSQL: mysqlConnect called.\n");

	lock (mysql_mutex);

	connection = mysql_init (NULL);
	if (!connection)
	{
		logger->log (" MYSQL: Failed to initialise MySQL connection.\n");
		release (mysql_mutex);
		return -1;
	}

	// Attempts to connect to the database
	connection = mysql_real_connect (connection, "localhost", db_user.c_str(), db_pass.c_str(), db_name.c_str(), 0, NULL, 0);
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
	logger->debug (DEBUG_STANDARD, " MYSQL: mysqlDisconnect called.\n");

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
	if (!connection)
	{
		logger->debug (DEBUG_MINIMAL, " MYSQL: Warning, connection not ready at start of query.\n");

		// Try to reconnect
		mysqlDisconnect ();
		mysqlConnect ();
		if (connection == NULL)
		{
			logger->log (" MYSQL: Failed to reconnect after a query was made without a connection ready to start with.");
			release (query_mutex);

			return NULL;
		}
	}

	// Builds the query
	va_list args;
	va_start (args, format);

	vsnprintf (query, 10240, format, args);

	MYSQL_RES* temp_result_set;

	logger->debugf (DEBUG_DETAILED, " MYSQL: mysqlQuery called with, %s.\n", query);

	lock (mysql_mutex);
	if (mysql_query (connection, query))
	{
		logger->debugf (DEBUG_MINIMAL, " MYSQL: Query failed, %s.\n", mysql_error (connection));

		// If the server has disconnected, reconnect and try again
		if ((mysql_errno (connection) == CR_SERVER_GONE_ERROR) || (mysql_errno (connection) == CR_SERVER_LOST))
		{
			release (mysql_mutex);
			mysqlDisconnect ();
			mysqlConnect ();
			lock (mysql_mutex);
			// If we failed to connect, wait a second and try one last time
			if (connection == NULL)
			{
				sleep (1);
				release (mysql_mutex);
				mysqlConnect ();
				lock (mysql_mutex);

				// If we still failed to connect drop out
				if (connection == NULL)
				{
					logger->log (" MYSQL: Failed to connect twice while running a query, dropping query.\n");
					va_end(args);
					release (mysql_mutex);
					release (query_mutex);

					return NULL;
				}
			}

			// Otherwise try the query again
			if (mysql_query (connection, query))
			{
				logger->debugf (DEBUG_MINIMAL, " MYSQL: Query failed again after reconnect, %s.\n", mysql_error (connection));
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


/**
 * Returns how many rows the last query affected
 */
unsigned long long MySQLHandler::mysqlAffectedRows (void)
{
	lock (mysql_mutex);
	unsigned long long affected_rows = mysql_affected_rows(connection);;
	release (mysql_mutex);
	return affected_rows;
}

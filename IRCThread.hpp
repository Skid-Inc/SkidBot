#ifndef	_IRC_Thread_H
#define _IRC_Thread_H

#define MAXDATAREAD	512
#define DEFAULT_IRC_PORT	6667

// Socket task defines
#define IRC_CONNECT	0
#define IRC_AUTH	1
#define IRC_RUNNING	2
#define IRC_CLOSE	3

// Global function prototypes
void *IRCThread (void *);
int send_command (std::string command, std::string data);
int send_room (std::string room, std::string message);
void *GIRCThread (void *);
int gsend_command (std::string command, std::string data);
int gsend_room (std::string room, std::string message);

#endif
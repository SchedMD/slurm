#ifndef SYSLOG_H 
#define SYSLOG_H

/*
** Makes sure the source has an appropriate registry entry for the 
** required facility. Also opens a handle to the appropriate log.
** A call to this should be made whenever a new logging thread begins.
**	source - name of app/thread which is logging events
**	facility - type of source - LOG_APP or LOG_SYS
**	returns TRUE-success FALSE-failure
** If a call is previously made to openlog in this thread, then openlog makes
** a call to closelog() before opening the new log
*/
int openlog(char* source,int facility);

/*
** Logs an entry into the previously opened log
**   priority - LOG_INFO or LOG_ERR or LOG_WARNING
**   logformat - printf style string followed by arguments
** If no call is previously made to openlog in the thread, then syslog makes a call
** to openlog("unknown_app",LOG_APP) before logging the event.
*/
int syslog(int priority,char* logformat,...);

/*
** Releases the handle to the previously opened log
** A call to this should be made whenever a logging thread exits
*/
int closelog(void);

/*
** priority values
*/
#define LOG_INFO	1 /* information event */
#define LOG_ERR		2 /* error       event */
#define LOG_WARNING 3 /* warning     event */

/*
** facility values
*/
#define LOG_APP		1 /* events logged to application log */
#define LOG_SYS		2 /* evnets logged to system      log */

#define LOG_INVALID 0 /* placeholder for invalid facility and priority entries */
#define MAX_LOG_MSG_SIZE 2048 /* maximum size of message that can be logged */

#endif


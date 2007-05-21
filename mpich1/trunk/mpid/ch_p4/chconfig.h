#ifndef CHCONFIG
#define CHCONFIG

#define MPID_HAS_HETERO

/* This makes chbrndv.c use memcpy for rendezvous messages to self */
#define MPID_RNDV_SELF

/* Turn on flow control */
/* #define MPID_NO_FLOW_CONTROL */
#define MPID_NO_FLOW_CONTROL 
#ifndef MPID_NO_FLOW_CONTROL
#define MPID_FLOW_CONTROL
#endif

/* Used for packet control */
#undef MPID_USE_SHMEM

/* Used for making sure we get the last packet from a tcp connection */
#define MPID_GET_LAST_PKT

#define MPID_HAS_PROC_INFO
/* This is needed if you want TotalView to acquire all of the processes
 * automagically.
 * If it is defined you must also define 
 *      int MPID_getpid(int index, char **hostname, char **imagename);
 * which takes an index in COMM_WORLD and returns the pid of that process as a result,
 * and also fills in the pointers to the two strings hostname (something which we can
 * pass to inet_addr, and image_name which is the name of the executable running
 * that this process is running.
 * You can fill in either (or both) pointers as (char *)0 which means
 * "the same as the master process".
 */

/* Put macro-definitions of routines here */
int p4_proc_info ( int, char **, char ** );
#define MPID_getpid(i,n,e) p4_proc_info((i),(n),(e))

/* Communicator initialization routines */
/* Comm_msgrep determines the common representation format for 
   members of the new communicator */
#define MPID_CommInit(oldcomm,newcomm) MPID_CH_Comm_msgrep( newcomm )
#define MPID_CommFree(comm)            MPI_SUCCESS

#endif

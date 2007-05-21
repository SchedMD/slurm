/* clog.h */

#ifndef _CLOG
#define _CLOG

#include <stdio.h>
#include "clog_time.h"		/* definitions of CLOG timer access rtns */

/* 
   the function of the CLOG logging routines is to write log records into 
   buffers, which are processed later.
*/

/* 
   CLOG buffers are linked lists of CLOG blocks, allocated as needed.
   Note that blocks are actually a little longer than CLOG_BLOCK_SIZE, which
   is the length of the data part
*/

/* mpich 1.1.2 and before's clog_block size is 1024 */
/*
   #define CLOG_BLOCK_SIZE 1024
*/
#define CLOG_BLOCK_SIZE 65536

typedef struct _CLOG_BLOCK {
    struct _CLOG_BLOCK *next;	/* next block */
    double data[CLOG_BLOCK_SIZE / sizeof (double) ];
} CLOG_BLOCK;
    
#define MAX_CLOG_BLOCKS 128

/* Formats of all records */

/* We distinguish between record types and event types (kinds), and have a
   small number of pre-defined record types, including a raw one.  We keep all
   records double-aligned for the sake of the double timestamp field.  Lengths
   are given in doubles.  Log records will usually consist of a CLOG_HEADER 
   followed by one of the types that follow it below, but record types
   CLOG_ENDBLOCK and CLOG_ENDLOG consist of the header alone. */

typedef struct {
    double timestamp;
    int rectype;
    int length;			/* in doubles */
    int procid;			/* currently rank in COMM_WORLD */
    int pad;			/* keep length a multiple of sizeof(dbl) */
    double rest[1];
} CLOG_HEADER;

typedef struct {
    int etype;			/* kind of message event */
    int tag;			/* message tag */
    int partner;		/* source or destination in send/recv */
    int comm;			/* communicator */
    int size;			/* length in bytes */
    int srcloc;			/* id of source location */
    double end[1];
} CLOG_MSG;

typedef struct {
    int etype;			/* type of collective event */
    int root;			/* root of collective op */
    int comm;			/* communicator */
    int size;			/* length in bytes */
    int srcloc;			/* id of source location */
    int pad;
    double end[1];
} CLOG_COLL;

typedef struct {
    int etype;			/* type of communicator creation */
    int parent;			/* parent communicator */
    int newcomm;		/* new communicator */
    int srcloc;			/* id of source location */
    double end[1];
} CLOG_COMM;

typedef char CLOG_CNAME[3 * sizeof(double)];
typedef char CLOG_DESC[2 * sizeof(double)];
typedef struct {
    int stateid;		/* integer identifier for state */
    int startetype;		/* starting event for state */
    int endetype;		/* ending event for state */
    int pad;
    CLOG_CNAME color;		/* string for color */
    CLOG_DESC description;	/* string describing state */
    double end[1];
} CLOG_STATE;

typedef struct {
    int etype;			/* event */
    int pad;
    CLOG_DESC description;	/* string describing event */
    double end[1];
} CLOG_EVENT;

typedef char CLOG_FILE[5 * sizeof(double)];
typedef struct {
    int srcloc;			/* id of source location */
    int lineno;			/* line number in source file */
    CLOG_FILE filename;		/* source file of log statement */
    double end[1];
} CLOG_SRC;

typedef struct {
    double timeshift;		/* time shift for this process */
    double end[1];
} CLOG_TSHIFT;

typedef struct {
    int etype;			/* raw event */
    int data;			/* uninterpreted data */
    int srcloc;			/* id of source location */
    int pad;
    CLOG_DESC string;   	/* uninterpreted string */
    double end[1];
} CLOG_RAW;

/* predefined record types (all include header) */

#define CLOG_ENDLOG    -2	/* end of log marker */
#define CLOG_ENDBLOCK  -1	/* end of block marker */
#define CLOG_UNDEF      0	/* something different */
#define CLOG_RAWEVENT   1	/* arbitrary record */
#define CLOG_MSGEVENT   2	/* message event */
#define CLOG_COLLEVENT  3	/* collective event */
#define CLOG_COMMEVENT  4	/* communicator construction/destruction  */
#define CLOG_EVENTDEF   5	/* event description */
#define CLOG_STATEDEF   6	/* state description */
#define CLOG_SRCLOC     7       /* identifier of location in source */
#define CLOG_SHIFT      8	/* time shift calculated for this process */

/* size to make sure there is always enough room in block for record plus
   trailer */
#define CLOG_MAX_REC_LEN ( 20 * sizeof(double) ) /* in bytes */
#define CLOG_MAXTIME 1000000.0	/* later than all times */

/* 
   log file types - currently old alog format for backward compatibility as
                    well as "native" clog format.
   Don't modify the following 3 #define statements, 
   unless mpe_log.c's MPE_Finish_log() is modified at the same time.
*/
#define CLOG_LOG        1
#define ALOG_LOG        2
#define SLOG_LOG        3

/* (abhi) memory requirement for SLOG */
#define SLOG_MEMORY_REQUIREMENT 2048

/* special event ids for ALOG compatibility */
#define LOG_MESG_SEND -101
#define LOG_MESG_RECV -102

/* special event type for defining constants */
#define LOG_CONST_DEF -201

/* predefined COMM event types */
#define INIT    101
#define DUP     102
#define SPLIT   103
#define CARTCR  104
#define COMMCR  105
#define CFREE   106


/* keep this larger than predefined event ids; it is for users */
#define CLOG_MAXEVENT 500

/* predefined state ids */
/* none */

/* keep this larger than predefined state ids; it is for users */
#define CLOG_MAXSTATE 200

/********************** global data structures ***************************/

#define CLOG_DIR_LEN 256
extern char   CLOG_outdir[CLOG_DIR_LEN];  /* directory where output will go */
extern int    CLOG_Comm;	/* Default communicator */
extern int    CLOG_status;	/* initialized? logging currently active? */
extern void   *CLOG_ptr;	/* pointer into current CLOG block */
extern void   *CLOG_block_end;	/* pointer to end of current CLOG block */
extern CLOG_BLOCK *CLOG_first, *CLOG_currbuff; /* blocks of buffer */
extern int    CLOG_intsperdouble, CLOG_charsperdouble;
extern int    CLOG_srcid;	/* next id for source code location */
extern int    CLOG_nextevent;	/* next id for user-defined events */
extern int    CLOG_nextstate;	/* next id for user-defined state  */
extern char   CLOG_filename[];	/* name for log file */
extern char   CLOG_tmpfilename[]; /* temp log file name (abhi) */ 
extern int    CLOG_tempFD;      /* temp log file descriptor (abhi) */
extern double *CLOG_out_buffer, *CLOG_left_buffer,
              *CLOG_right_buffer;    /* buffers for clog-merge (abhi) */ 
extern int    CLOG_num_blocks;
extern long   CLOG_event_count;
extern void   *slog_buffer;
/************************* function prototypes ***************************/

void CLOG_Init ( void );
void CLOG_Finalize ( void );
void CLOG_put_hdr ( int );
void CLOG_LOGMSG ( int, int, int, int, int );
void CLOG_LOGSRCLOC ( int , int, char * );
void CLOG_LOGCOLL ( int, int, int, int );
void CLOG_LOGRAW ( int, int, char * );
void CLOG_LOGTIMESHIFT ( double );
void CLOG_LOGCOMM ( int, int, int );
void CLOG_LOGSTATE ( int, int, int, char *, char * );
void CLOG_LOGEVENT ( int, char * );
void CLOG_LOGENDBLOCK ( void );
void CLOG_LOGENDLOG ( void );
void CLOG_newbuff ( CLOG_BLOCK **);
int  CLOG_get_new_event ( void );
int  CLOG_get_new_state ( void );
void CLOG_setup ( void );
void CLOG_init_tmpfilename ( void );
void CLOG_nodebuffer2disk ( void );
void CLOG_init_buffers ( void );

/*********************** macros *******************************************/

/* for testing CLOG_status, one bit for initialized and one for on/off 
   0 - data structures are initialized and logging is ON
   1 - data structures are initialized and logging is OFF
   2 - data structures are not initialized, logging on; error
   3 - data structures are not initialized, logging off; error even so
   */

#define CLOG_OK     (!CLOG_status)
#define CLOG_SKIP   (CLOG_status==1)
#define CLOG_ERROR  (CLOG_status==2 || CLOG_status==3)

#define CLOG_not_init (fprintf(stderr, "CLOG used before being initialized\n"))

#endif /* _CLOG */




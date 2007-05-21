#include <stdio.h>
#include "usc.h"

#define MAX_DIRNAME_LEN 	100
#define MAX_LOG_STRING_LEN 	12
#define MAX_BUF_SIZE       	100
#define ALOG_LOGFILE		"alogfile.p"

#define ALOG_TRUNCATE		0
#define ALOG_WRAP		1

#define ALOG_OFF		0
#define ALOG_ON			1

#define ALOG_EVENT_SYNC        -101
#define ALOG_EVENT_PAIR_A1     -102
#define ALOG_EVENT_PAIR_A2     -103
#define ALOG_EVENT_PAIR_B1     -104
 

struct trace_buf;  /* for c++ folks; defined below */
struct head_trace_buf {
        int             next_entry;
        int             max_size;
        unsigned long   prev_time;
        unsigned long   ind_time;
        int             trace_flag;
        struct trace_buf *xx_list;
        struct trace_buf *cbuf;
        FILE            *file_t;
};

struct trace_buf {
        struct trace_buf *next_buf;
        struct trace_table {
                int     id;
                int     task_id;
                int     event;
                int     data_int;
                char    data_string[MAX_LOG_STRING_LEN+1];
                unsigned long     tind;
                unsigned long     tstamp;
        } ALOG_table[MAX_BUF_SIZE];
};

extern int xx_alog_status;
extern int xx_alog_setup_called;
extern int xx_alog_output_called;
extern char xx_alog_outdir[];
extern struct head_trace_buf *xx_buf_head;

/* 
 * Function declarations 
 */

VOID xx_write (struct head_trace_buf *,int,int,int, char *), 
     xx_dump (struct head_trace_buf *), 
     xx_dump_aux (struct trace_buf *, FILE *, int, int),
     xx_user (struct head_trace_buf *, int), 
     xx_user1 (struct head_trace_buf *, int), 
     xx_alog_setup (int,int);
int  xx_getbuf (struct head_trace_buf *);


#ifdef ALOG_TRACE

#define ALOG_DEC

#define ALOG_STATUS(status) \
	if ((status) == ALOG_ON) \
		xx_alog_status |= 0x1; \
	else \
		xx_alog_status &= ~0x1

#define ALOG_ENABLE ALOG_STATUS(ALOG_ON)

#define ALOG_DISABLE ALOG_STATUS(ALOG_OFF)

#define ALOG_SETDIR(dir) \
	{\
	strncpy(xx_alog_outdir,(dir),MAX_DIRNAME_LEN); \
	xx_alog_outdir[MAX_DIRNAME_LEN] = '\0'; \
	}


#define ALOG_SETUP(pid,flag) \
        {\
            if (xx_alog_status & 0x1 &&  !xx_alog_setup_called) \
	    {\
                xx_alog_setup_called = 1;\
                xx_alog_setup((pid),(flag));\
	    }\
        }

#define ALOG_MASTER(pid,flag) \
	{\
	    if (xx_alog_status & 0x1) \
	    {\
	        xx_alog_setup((pid),(flag)); \
	        xx_user1(xx_buf_head,(pid)); \
	    }\
	}

#define ALOG_DEFINE(event,edef,strdef) \
        {\
        if (xx_alog_status & 0x1) \
        {\
            xx_write(xx_buf_head,0,(-9),(event),(edef)); \
            xx_write(xx_buf_head,0,(-10),(event),(strdef)); \
        }\
        }

#define ALOG_LOG(pid,type,data1,data2) \
	{\
	if (xx_alog_status & 0x1) \
		xx_write(xx_buf_head,(pid),(type),(data1),(data2)); \
	}

#define ALOG_OUTPUT \
        {\
            if (xx_alog_status & 0x1  &&  !xx_alog_output_called) \
	    {\
                xx_alog_output_called = 1;\
                xx_dump(xx_buf_head);\
	    }\
        }

#else

#define ALOG_DEC 
#define ALOG_STATUS(a)
#define ALOG_ENABLE
#define ALOG_DISABLE
#define ALOG_SETDIR(a)
#define ALOG_SETUP(a,b)
#define ALOG_MASTER(a,b)
#define ALOG_DEFINE(a,b,c)
#define ALOG_LOG(a,b,c,d)
#define ALOG_OUTPUT

#endif

#if !defined(HAVE_GETWD)
/* At least for some of these systems, we can use getcwd for getwd */
#if defined(IPSC860)      || defined(DELTA)        || \
    defined(PARAGON)      ||                          \
    defined(TITAN)        || defined(SYMMETRY_PTX) || \
    defined(HP)           || defined(NCUBE)
#define getwd(X)  getcwd(X,sizeof(X))
#endif
#endif

#if defined(CRAY) || defined(TITAN)  ||  defined(NCUBE)
#define alogfmaster_  ALOGFMASTER
#define alogfsetup_   ALOGFSETUP
#define alogfdefine_  ALOGFDEFINE
#define alogflog_     ALOGFLOG
#define alogfoutput_  ALOGFOUTPUT
#define alogfstatus_  ALOGFSTATUS
#define alogfsetdir_  ALOGSETDIR
#define alogfenable_  ALOGFENABLE
#define alogfdisable_ ALOGFDISABLE
#endif


#if defined(NEXT)  ||  defined(HP)
#define alogfmaster_  alogfmaster
#define alogfsetup_   alogfsetup
#define alogfdefine_  alogfdefine
#define alogflog_     alogflog
#define alogfoutput_  alogfoutput
#define alogfstatus_  alogfstatus
#define alogfsetdir_  alogsetdir
#define alogfenable_  alogfenable
#define alogfdisable_ alogfdisable
#endif


#ifndef _CLOGIMPL
#define _CLOGIMPL

#if defined(NEEDS_STDLIB_PROTOTYPES) && !defined ( malloc )
#include "protofix.h"
#endif

#include "clog.h"

#if defined(MPIR_MEMDEBUG)
/* Enable memory tracing.  This requires MPICH's mpid/util/tr2.c codes */
#include "mpimem.h"		/* Chameleon memory debugging stuff */
#define MALLOC(a)    MPID_trmalloc((unsigned)(a),__LINE__,__FILE__)
#define FREE(a)      MPID_trfree(a,__LINE__,__FILE__)
#else
#define MALLOC(a)    malloc(a)
#define FREE(a)      free(a)
#define MPID_trvalid(a)
#endif


void CLOG_dumplog ( void );
void CLOG_outblock (double *);
void CLOG_dumpblock ( double * );
int  CLOG_reclen ( int );
void CLOG_msgtype ( int );
void CLOG_commtype ( int );
void CLOG_colltype ( int );
void CLOG_rectype ( int );

void adjust_CLOG_HEADER ( CLOG_HEADER * );
void adjust_CLOG_MSG ( CLOG_MSG * );
void adjust_CLOG_COLL ( CLOG_COLL * );
void adjust_CLOG_COMM ( CLOG_COMM * );
void adjust_CLOG_STATE ( CLOG_STATE * );
void adjust_CLOG_EVENT ( CLOG_EVENT * );
void adjust_CLOG_SRC ( CLOG_SRC * );
void adjust_CLOG_RAW ( CLOG_RAW * );

void CLOGByteSwapDouble (double *, int);
void CLOGByteSwapInt (int *, int);

#endif




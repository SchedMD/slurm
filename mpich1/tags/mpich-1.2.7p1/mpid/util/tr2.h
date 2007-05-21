#ifndef _TRALLOC
#define _TRALLOC

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the library directory (lib/$ARCH/$COMM) corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif

/* Define MPIR_MEMDEBUG to enable these memory tracing routines */

#if defined(MPIR_MEMDEBUG) || defined(_TR_SOURCE)
#define MALLOC(a)    MPID_trmalloc((unsigned)(a),__LINE__,__FILE__)
#define CALLOC(a,b)  \
    MPID_trcalloc((unsigned)(a),(unsigned)(b),__LINE__,__FILE__)
#define FREE(a)      MPID_trfree(a,__LINE__,__FILE__)
#define NEW(a)        (a *)MALLOC(sizeof(a))
#define STRDUP(a)    MPID_trstrdup(a,__LINE__,__FILE__)

void MPID_trinit ( int );
void *MPID_trmalloc ( unsigned int, int, char * );
void MPID_trfree ( void *, int, char * );
int MPID_trvalid ( char * );
void MPID_trspace ( int *, int * );
void MPID_trdump ( FILE * );
void MPID_trSummary ( FILE * );
void MPID_trid ( int );
void MPID_trlevel ( int );
void MPID_trpush ( int );
void MPID_trpop (void);
void MPID_trDebugLevel ( int );
void *MPID_trstrdup( const char *, int, const char * );
void *MPID_trcalloc ( unsigned, unsigned, int, char * );
void *MPID_trrealloc ( void *, int, int, char * );
void MPID_trdumpGrouped ( FILE * );
void MPID_TrSetMaxMem ( int );
#else
/* Should these use size_t for ANSI? */
#define MALLOC(a)    malloc((unsigned)(a))
#define CALLOC(a,b)  calloc((unsigned)(a),(unsigned)(b))
#define FREE(a)      free((void *)(a))
#define NEW(a)    (a *)MALLOC(sizeof(a))
#define STRDUP(a)    strdup(a)
#endif

#endif

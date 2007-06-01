/*
 *  $Id: sbcnst2.h,v 1.3 2001/10/19 22:01:19 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#ifndef _SBCNST
#define _SBCNST

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the library directory (lib/$ARCH/$COMM) corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif

#include <stdio.h>

typedef struct _MPID_SBHeader *MPID_SBHeader;
extern MPID_SBHeader MPID_SBinit ( int, int, int );
extern void  MPID_SBfree ( MPID_SBHeader, void * ),
            *MPID_SBalloc ( MPID_SBHeader ),
             MPID_SBPrealloc ( MPID_SBHeader, int ),
             MPID_SBdestroy ( MPID_SBHeader ),
             MPID_SBrelease ( MPID_SBHeader, void * ),
             MPID_SBFlush ( MPID_SBHeader ),
             MPID_SBDump ( FILE *, MPID_SBHeader ),
             MPID_SBReleaseAvail ( MPID_SBHeader ),
             MPID_SBvalid ( MPID_SBHeader );

/* Chameleon/PETSc includes memory tracing functions that can be used
   to track storage leaks.  This code chooses that or the copy that 
   has been placed into mpich/util/tr.c 
 */
#ifndef MALLOC

#if defined(MPIR_MEMDEBUG)
/* Use MPI tr version of MALLOC/FREE */
#include "tr2.h"
/* Also replace the SB allocators so that we can get the trmalloc line/file
   tracing. */
#define MPID_SBinit(a,b,c) ((void *)(a))
#define MPID_SBalloc(a)    MPID_trmalloc((unsigned)(a),__LINE__,__FILE__)
#define MPID_SBfree(a,b)   MPID_trfree((char *)(b),__LINE__,__FILE__)
#define MPID_SBdestroy(a)
#else

/* We also need to DECLARE malloc etc here.  Note that P4 also declares
   some of these, and thus if P4 in including this file, we skip these
   declarations ... */
#ifndef P4_INCLUDED

#if HAVE_STDLIB_H || STDC_HEADERS
#include <stdlib.h>

#else
#ifdef __STDC__
extern void 	*calloc(/*size_t, size_t*/);
extern void	free(/*void * */);
extern void	*malloc(/*size_t*/);
#elif defined(MALLOC_RET_VOID)
extern void *malloc();
extern void *calloc();
#else
extern char *malloc();
extern char *calloc();
/* extern int free(); */
#endif /* __STDC__ */
#endif /* HAVE_STDLIB_H || STDC_HEADERS */
#endif /* !defined(P4_INCLUDED) */

#define MALLOC(a)    malloc((unsigned)(a))
#define CALLOC(a,b)  calloc((unsigned)(a),(unsigned)(b))
#define FREE(a)      free((char *)(a))
#define NEW(a)    (a *)MALLOC(sizeof(a))
#endif /*MPIR_MEMDEBUG*/
#endif /*MALLOC*/

#endif

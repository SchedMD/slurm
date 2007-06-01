/* $Id: tools.h,v 1.2 2000/07/11 22:03:35 gropp Exp $ */

/*
    This file contains some basic definitions that the tools routines
    may use.  They include:

    The name of the storage allocator
    The error routine/value to use
    Registered Error values
 */    
#ifndef __TOOLS
#define __TOOLS

#if defined(HPUX) && !defined(_C_ADDR_T)
#define _CADDR_T
typedef char *caddr_t;
#endif

/*
   There are many things that a user may want to configure when building tools.
   This gives a standard hook for the LIBRARY MAINTAINGER to modify parts of
   PETSc by modifying this configuration file.
 */
#include "petsccfg.h"

/*
   Definitions used for the Fortran interface.
 */    
#if defined(titan) || defined(cray) || defined(ncube)
#define F77_NAME_UPPER
#elif !defined(rs6000) && !defined(NeXT) && !defined(HPUX)
#define F77_NAME_LOWER_USCORE
#endif

/*
   Some systems are most easily handled by assuming strict ANSI-C runtimes
 */
#if defined(HPUX) || defined(MSDOS) || defined(cray)
#define STRICT_ANSI_C
#endif

/* 
   Some systems have a standard header file stdlib.h ; others do not.
   We try to determine who does here 
 */
#if defined(IRIX) || defined(rs6000) || defined(dec5000) || defined(ipsc2)
#define NOSTDHDR
#endif
#if defined(sun4) && defined(sun4Pre41)
#define NOSTDHDR
#endif

/*
  The following variables are used by tools but are normally not
  needed by User programs.
  
  OSMajorVersion
  OSMinorVersion

  These are needed only in interfacing to system routines where the
  system definition is undergoing rather radical changes.  Sun4's and
  rs6000's are such systems; for the sun4's, the biggest change is
  between 4.0.x and 4.1.1; for rs6000's, there was a big change between
  AIX 3.1 and 3.2.
 */
#if !defined(OSMajorVersion)
#define OSMajorVersion 100
#endif
#if defined(sun4) && (OSMajorVersion < 4 || (OSMajorVersion == 4 && \
    OSMinorVersion == 0) )
#define sun4Pre41
#endif

/* This may be a problem, since NULL may be declared elsewhere (for example,
   this did not work on the next) */
#if defined(dec_alpha) && !defined(stdout)
#include <stdio.h>

#elif defined(cray) && !defined(_STDIO_H)
#include <stdio.h>

#elif (defined(rs6000) || defined(intelnx)) && !defined(stdout)
#include <stdio.h> 

#elif !defined(NeXT) || (defined(NeXT) && !defined(_STDIO_H))
/* Just in case we are an ANSI C compiler (see the prototypes for trfree 
   below) */
#include <stdio.h>
#endif

#ifdef FAST_MALLOC
/* Try to be careful to not define malloc if it was already defined */
#if defined(rs6000) && defined(_H_STDLIB) && defined(_ANSI_C_SOURCE)
#else

#if defined(rs6000) || defined(intelnx) || defined(cray) || \
    !defined(NOSTDHDR)
#include <stdlib.h>
#elif defined(solaris)
extern void *malloc(), *calloc(), *realloc();
#else
extern char *malloc(), *calloc(), *realloc();
#endif


#define MALLOC_DEFINED
#endif /* defined(rs6000) and special cases */

#define MALLOC(a)    malloc((unsigned)(a))
#define FREE(a)      free((char *)(a))
#define CALLOC(a,b)    calloc((unsigned)(a),(unsigned)(b))
#define REALLOC(a,b)   realloc(a,(unsigned)(b))
#define TRDUMP(a) 
#define TRSPACE(a,b) {*(a)=0;*(b)=0;}
#define TRID(a)
#define TRPUSH(a)    
#define TRPOP
#define TRVALID(str)

#else
#ifdef __STDC__
extern void *trmalloc(unsigned int a, int lineno, char *fname), 
            trfree(char *a, int line, char *file), 
            trpush(int a), 
            trpop(void), 
            trspace(int *space, int *fr), 
            trdump(FILE *fp), 
            *trcalloc(unsigned nelem, unsigned elsize, 
		      int lineno, char *fname), 
            *trrealloc(void *p, int size, int lineno, char *fname);
extern int  trvalid(char *str);
#else
extern void *trmalloc(), trfree(), trpush(), trpop(), trspace(), 
            trdump(), *trcalloc(), *trrealloc();
extern int  trvalid();
#endif
#define MALLOC(a)    trmalloc((unsigned)(a),__LINE__,__FILE__)
#define CALLOC(a,b)    trcalloc((unsigned)(a),(unsigned)(b),__LINE__,__FILE__)
#define REALLOC(a,b)   trrealloc(a,(unsigned)(b),__LINE__,__FILE__)

#ifdef DEBUG_ALL
#define FREE(a)      {trfree((char *)(a),__LINE__,__FILE__);\
			  if (GETERR){DEBUGTBC("Free");__terrno.err=0;}}
#else
#define FREE(a)      trfree((char *)(a),__LINE__,__FILE__)
#endif /* DEBUG_ALL */

#define TRDUMP(a)    trdump(a)
#define TRSPACE(a,b) trspace(a,b)
#define TRID(a)      trid(a)
#define TRPUSH(a)    trpush(a)
#define TRPOP        trpop()
#define TRVALID(str) trvalid(str)
#endif /* FAST_MALLOC */

#define NEW(a)    (a *)MALLOC(sizeof(a))
/* In order to help track storage use, different parts of the the "tools"
   package use different TRid's: 
   (We should switch to -i*512 so that we can have more and mask them off.
   The representation should be:
        (package)(common ops)(specific routine)
   with bits defined here for the first two.
 */
#define BCTRID (-(0x100))
#define SPTRID (-(0x200))
#define CMTRID (-(0x300))
#define ITTRID (-(0x400))
#define SVTRID (-(0x500))

#define TRIDCREATE (0x20)
#define TRIDPOOL   (0x40)
#define TRIDREG    (0x60)

/*M
    MEMCPY - copy non-overlapping memory

    Synopsis:
    MEMCPY(s1,s2,n)
    void *s1, *s2;
    int  n;

    Input Parameters:
.   s1 - destination of copy
.   s2 - source for copy
.   n  - number of bytes to copy
M*/
#define MEMCPY(s1,s2,n) memcpy((char*)(s1),(char*)(s2),n)
#define MEMSET(s,c,n)   memset((char*)(s),c,n)

/* Scalar data type.  By default, it is double (except on Cray) 
   NOTE: These are slowly being introduced into the tools files; do not
   count on them yet. */
#ifndef cray
typedef double SCALAR;
#define LAPACK_DOUBLE 1
#else
typedef float SCALAR;
#define LAPACK_DOUBLE 0
#endif

/* Complex data types.  Note that some <math.h> (intelparagon for one)
   define these */
#if !defined(intelparagon) && !defined(_MATH_H_)
typedef struct {
    float r, i;
    } complex;
#endif
typedef struct {
    double r, i;
    } dcomplex;

/* 
   These definitions provide a way to select between single and double
   precision versions of the LAPACK routines used in PETSc.
 */
#if LAPACK_DOUBLE == 0
#define LAgemv_  sgemv_
#define LAgetrf_ sgetrf_
#define LAgetrs_ sgetrs_
#define LAgemv_  sgemv_
#define LAtrmv_  strmv_
#define LAtrsl_  strsl_
#define LAZgemv_ cgemv_
#define LAZtrmv_ ctrmv_
#define LAZtrsl_ ctrsl_
#define LAger_   sger_

#else
#define LAgemv_  dgemv_
#define LAgetrf_ dgetrf_
#define LAgetrs_ dgetrs_
#define LAgemv_  dgemv_
#define LAtrmv_  dtrmv_
#define LAtrsl_  dtrsl_
#define LAZgemv_ zgemv_
#define LAZtrmv_ ztrmv_
#define LAZtrsl_ ztrsl_
#define LAger_   dger_
#endif

/* Finally, should add the code for FORTRANCAPS and FORTRANUNDERSCORE for the
   LAPACK routines */
#if defined(F77_NAME_UPPER)
#define dgemv_  DGEMV
#define dgetrf_ DGETRF
#define dgetrs_ DGETRS
#define dger_   DGER
#define sgemv_  SGEMV
#define sgetrf_ SGETRF
#define sgetrs_ SGETRS
#define sger_   SGER
#define dtrmv_  DTRMV
#define dtrsl_  DTRSL
#define zgemv_  ZGEMV
#define ztrmv_  ZTRMV
#define ztrsl_  ZTRSL
#define strmv_  STRMV
#define strsl_  STRSL
#define cgemv_  CGEMV
#define ctrmv_  CTRMV
#define ctrsl_  CTRSL
#elif !defined(F77_NAME_LOWER_USCORE)
#define dgemv_  dgemv
#define dgetrf_ dgetrf
#define dgetrs_ dgetrs
#define dger_   dger
#define sgemv_  sgemv
#define sgetrf_ sgetrf
#define sgetrs_ sgetrs
#define sger_   sger
#define dtrmv_  dtrmv
#define dtrsl_  dtrsl
#define zgemv_  zgemv
#define ztrmv_  ztrmv
#define ztrsl_  ztrsl
#define strmv_ strmv
#define strsl_ strsl
#define cgemv_ cgemv
#define ctrmv_ ctrmv
#define ctrsl_ ctrsl
#endif

/* Many programs currently use qsort.  Different systems have different ideas 
   about the types of the parameters to qsort, and the rs6000 in particular is
   very picky.
 */
#if defined(rs6000)
#define QSORT(b,n,s,f) qsort((void*)b,(size_t)n,(size_t)s,(int (*)())f)
#else
#define QSORT(b,n,s,f) qsort((char*)b,(int)n,(int)s,(int (*)())f)
#endif

/* Error values and handler */
/*D
   Error-handling macros for the tools package.

   Description:
   These macros provide for a simple method to set and check error values.
   The basic macro to set an error is
$   SETERR(error_value)

   To check an error value, use GETERR ; this has value 0 if there are no
   errors and non-zero otherwise.

   A more sophisticated macro is  CHKERRVC(l,v,s) which check the error
   value, and if it is set, add a local error value "l" (an int), a local error
   message "s" (a (char *)), and returns the value v.  CHKERRC(l,s) is the
   same, but just returns (for void functions).

   In the macros below, the following conventions are made for the
   parameter names:
.   a - global error value (int)
.   b - boolean condition/value
.   l - local error value  (int)
.   s - error message (char *)
.   v - value (to be returned) (any type)

$ SETERR(a)      - set the error value
$ SETERRC(a,s)   - ditto, with a string
$ SETERRB(b,a)   - if b, set error
$ SETERRBC(b,a,s)- ditto with a string
$ GETERR         - get the error value
$ CHKERR(l)      - check the error value and return if set
$ CHKERRC(l,s)   - ditto, with a string
$ CHKERRV(l,v)   - as CHKERR, but return a value
$ CHKERRVC(l,v,s)- ditto, with a string
$ CHKERRN(l)     - as CHKERRV(l,0)
$ CHKERRNC(l,s)  - as CHKERRVC(l,0,s)

$ CHKPTR(p)      - if p is null, set nomem and return
$ CHKPTRC(p,s)   - ditto, with string
$ CHKPTRV(p,v)   - ditto, return value
$ CHKPTRN(p)     - ditto, return null
$ CHKPTRVC(p,v,s)- ditto, with string and return value
  
 The macros __LINE__ and __FILE__ (defined by the cpp preprocessor) may
 be used to generate a traceback.

 The error macros are NOPs unless DEBUG_ALL is defined.
 To get a traceback (file and line #), also define DEBUG_TRACEBACK
D*/

/* We make this definition global so that optimized and unoptimized
   libraries may be mixed together */
typedef struct __TOOLSERROR {
    int err;
    /* Other stuff will go here; DO NOT USE IT */
    } __ToolsError;
extern __ToolsError __terrno;

#define SETERRNOMEM SETERRC(ERR_NO_MEM,"Out of Memory")

#ifdef DEBUG_TRACEBACK

#define DEBUGTBF(line,file) fprintf( stderr, "Line %d in %s\n", line, file )
#define DEBUGTBCF(line,file,c) \
    fprintf( stderr, "Line %d in %s: %s\n", line, file, c )
#else
#define DEBUGTBF(line,file)
#define DEBUGTBCF(line,file,c)
#endif

#define DEBUGTB DEBUGTBF(__LINE__,__FILE__)
#define DEBUGTBC(c) DEBUGTBCF(__LINE__,__FILE__,c)

#ifdef DEBUG_ALL

#define SETERR(a)         {__terrno.err = a;DEBUGTB;}
#define SETERRC(a,s)      {__terrno.err = a;DEBUGTBC(s);}
#define SETERRB(b,a)      {if(b)SETERR(a)}
#define SETERRBC(b,a,s)   {if(b)SETERRC(a,c)}
#define GETERR            (__terrno.err)
#define CHKERR(l)         {if (GETERR){DEBUGTB;return;}}
#define CHKERRC(l,s)      {if (GETERR){DEBUGTBC(s);return;}}
#define CHKERRV(l,v)      {if (GETERR){DEBUGTB;return v;}}
#define CHKERRVC(l,v,s)   {if (GETERR){DEBUGTBC(s);return v;}}
#define CHKERRN(l)        CHKERRV(l,0)
#define CHKERRNC(l,s)     CHKERRVC(l,0,s)

#else
#define SETERR(a) 
#define SETERRC(a,s)
#define SETERRB(b,a)
#define SETERRBC(b,a,s)
#define GETERR 0
#define CHKERR(l)
#define CHKERRC(l,s)
#define CHKERRV(l,v)
#define CHKERRVC(l,v,s)
#define CHKERRN(l)
#define CHKERRNC(l,s)
#endif

#define CHKPTR(p)        if (!p) {SETERRNOMEM;return;}
#define CHKPTRC(p,s)     if (!p) {SETERRC(ERR_NO_MEM,s);return;}
#define CHKPTRV(p,v)     if (!p) {SETERRNOMEM;return v;}
#define CHKPTRN(p)       if (!p) {SETERRNOMEM;return 0;}
#define CHKPTRVC(p,v,s)  if (!p) {SETERRC(ERR_NO_MEM,s);return v;}

#define ERR_NONE   0
#define ERR_NO_MEM 1

#define ERR_BASE_SPARSE 0x1000
#define ERR_BASE_ITER   0x2000
#define ERR_BASE_XTOOLS 0x3000
#define ERR_BASE_DDSM   0x4000
#define ERR_BASE_BC     0x5000

#endif

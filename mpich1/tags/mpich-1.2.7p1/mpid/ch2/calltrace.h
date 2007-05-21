/*
 * These macros allow the program to generate its own trace information
 * The macros can be defined as null for performance-critical operation.
 * These are under construction and will probably change.
 *
 * The basic idea is that on entry to a routine,
 *   TR_SET_NAME("routine name")
 * is called, and either
 *   TR_POP
 *   RETURN
 *   RETURNV(val)
 * are called on exit from the routine.
 * 
 */

#ifndef TR_POP 

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the library directory (lib/$ARCH/$COMM) corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif

/* needed for FILE below */
#include <stdio.h>

#ifdef DEBUG_TRACE
/* Should just put on a call stack */
#define TR_MAX_STACK 128
extern char *(TR_stack[TR_MAX_STACK]);
extern int  TR_stack_sp, TR_stack_debug;
#define TR_PUSH(a) { if (TR_stack_debug) fprintf( stderr, "[%d]Entering %s\n",MPID_MyWorldRank, a);\
    if (TR_stack_sp < TR_MAX_STACK) TR_stack[TR_stack_sp++] = a;}
#define TR_POP { if (TR_stack_debug) fprintf( stderr, "[%d]Exiting %s\n", \
				MPID_MyWorldRank, TR_stack[TR_stack_sp-1]);\
                 if (TR_stack_sp > 0) TR_stack_sp--;}
#define RETURN { TR_POP; return ; }
#define RETURNV(val) { TR_POP; return val ; }
void TR_stack_print ( FILE *, int );

#else
#define TR_PUSH(a)
#define TR_POP
#define RETURN return
#define RETURNV(val) return val
#endif
void TR_stack_init ( int );
#endif


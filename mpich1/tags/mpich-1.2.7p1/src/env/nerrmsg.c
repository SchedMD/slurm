/* Include the configure definitions now */
#ifndef MPICHCONF_INC
#define MPICHCONF_INC
#include "mpichconf.h"
#endif

#ifdef VXWORKS
#include <types/vxANSI.h>
#endif
#include <stdarg.h>
#include <stdlib.h>
#include "mpiimpl.h"

/* Don't process this file to find error messages: */
/* ###Exit GetErrMsgs### */

/* Local Prototypes */
char *MPIR_GetNLSMsg( int, int, char * );
const char *MPIR_Get_error_string( int errcode );
/* void MPIR_GetErrorMessage( int, char *, char ** );*/

/* We actually have 18 bits for the BIGRING, so the size can be 256K */
#define MAX_ERROR_RING 16
#define MAX_ERROR_BIGRING 8192


/* 
 * Debugging flags.  Turn debugging on by 
 *   setenv MPICH_DEBUG_ERRS
 */

static int DebugFlag = 0;
static int DebugCheck = 0;

static void _CheckForDebug(void)
{
    if (DebugCheck) return;
    if (getenv("MPICH_DEBUG_ERRS")) DebugFlag = 1;
    DebugCheck = 1;
}
static void _PrintErrCode( int errcode )
{
    int errclass, errkind, ringidx, bigringidx;
    
    errclass   = errcode & MPIR_ERR_CLASS_MASK;
    errkind    = (errcode & MPIR_ERR_CODE_MASK) >> MPIR_ERR_CLASS_BITS;
    bigringidx = errcode >> MPIR_ERR_CODE_BITS;
    ringidx    = (bigringidx % MAX_ERROR_RING);

    PRINTF( "errcode %x = %d %d %d %d\n", errcode, errclass, errkind, 
	    ringidx, bigringidx );
}

/*
 * This file implements a general error message handler.
 * The calling sequence is
 *
 * MPIR_Err_setmsg( errclass, errkind, routinename, generic_string, 
 *                  message_string, ... )
 *
 * where errkind is the "variety" of the error class (this is used to
 * form the code, but is not the only contributed to the code).
 * The routinename and default string may be null.  The default string is
 * in printf format, and the args to the printf follow.
 * The default string may be null, in which case a predefined value is used.
 */

/* See MPI-2 8.5: MPI_MAX_ERROR_STRING does not include terminating null */
static char error_ring[MAX_ERROR_RING][MPI_MAX_ERROR_STRING+1];
static int  error_ring_idx[MAX_ERROR_RING];

/* We make this volatile incase we are running in a multi-threaded 
   enviroment.  Some older compilers can't handle volatile. */
#ifndef HAS_VOLATILE
#define volatile
#endif
static volatile int error_big_ring_pos=1;

/*
 */
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
int MPIR_Err_setmsg( int errclass, int errkind,
		     const char *routine_name, 
		     const char *generic_string, 
		     const char *default_string, ... )
{
    int error_ring_pos, error_ring_id;
    const char *format;     /* Even kind */
    const char *def_format; /* Odd kind */
    va_list Argp;

    va_start( Argp, default_string );

#else
/* This assumes old-style varargs support */
int MPIR_Err_setmsg( errclass, errkind, routine_name, 
		     generic_string, default_string, va_alist )
int errclass, errkind;
const char *routine_name, *generic_string, *default_string;
va_dcl
{
    int error_ring_pos, error_ring_id;
    const char *format;     /* Even kind */
    const char *def_format; /* Odd kind */
    va_list Argp;

    va_start( Argp );
#endif

    _CheckForDebug();

    /* thread lock */
    error_ring_id = error_big_ring_pos++;
    if (error_big_ring_pos > MAX_ERROR_BIGRING) error_big_ring_pos = 1;
    /* thread unlock */
    error_ring_pos = (error_ring_id % MAX_ERROR_RING);
    /* thread unlock */

    /* If errkind is ODD, and the number of arguments is > 0, then 
       we want to try for the EVEN errkind (1+ the cvalue from the code).
       Otherwise, we want to use the default message (the input errkind) 
     */
#ifdef FOO
    /* To find the number of arguments, we need to look for a void*0? */
    /* We actually need to know only if there ARE any arguments */
    /* _numargs() is a Crayism that would be useful here */
    if (_numargs() > 5) {
	if (errkind & 0x1) errkind++;
    }
#endif
    /* In the odd kind case, get the two messages */
    if (errkind & 0x1) {
#if defined(USE_NLS_CAT)
	def_format = MPIR_GetNLSMsg( errclass, errkind, default_string );
	format     = MPIR_GetNLSMsg( errclass, errkind+1, default_string );
#else
	def_format = (default_string) ? (default_string) : 
	    MPIR_Get_error_string( MPIR_ERRCLASS_TO_CODE(errclass,errkind) );
	format = MPIR_Get_error_string( MPIR_ERRCLASS_TO_CODE(errclass,errkind+1) );
#endif
    }
    else {
#if defined(USE_NLS_CAT)
	format = MPIR_GetNLSMsg( errclass, errkind, default_string );
#else
	if (!default_string) 
	    format = MPIR_Get_error_string( MPIR_ERRCLASS_TO_CODE(errclass,errkind) );
	else
	    format = default_string;
#endif
	/* Here is a fallback for no message string */
	if (!format)
	    format = generic_string;
    }
    /* Use format if there are args, else use def_format */
    /* We need to replace this with code that is careful about the buffer
       lengths.  There is code like this in errmsg.c */
    /* Grrr.  There is no easy way to see if there *ARE* any args.  
       We need to place a boolean in the stdargs list that tells us
       whether there are more values. */
    if (0) {
	strcpy( error_ring[error_ring_pos], def_format );
    }
    else {
	if (format) {
	    vsprintf( error_ring[error_ring_pos], format, Argp );
	}
	else {
	    if (def_format) strcpy( error_ring[error_ring_pos], def_format );
	    else strcpy (error_ring[error_ring_pos], "No error message" );
	}
    }
    error_ring_idx[error_ring_pos] = error_ring_id;
    va_end( Argp );

    if (DebugFlag) {
	PRINTF( "Placed message (%d,%d) %s in %d\n", 
		errclass, errkind, error_ring[error_ring_pos], 
		error_ring_id );
    }
    return errclass | (errkind << MPIR_ERR_CLASS_BITS) |
	(error_ring_id << (MPIR_ERR_CODE_BITS));
}

/*
 * This routine maps a code that contains a reference to the error ring
 * to the error ring text.  If the code does not refer to the ring, 
 * it return null.  If it does refer to the ring, but the value is lost
 * (we've circled the ring at least once), it also returns null.
 */
char * MPIR_Err_map_code_to_string( int errcode )
{
    int ring_pos, big_ring_pos;

    _CheckForDebug();

    big_ring_pos = errcode >> MPIR_ERR_CODE_BITS;
    if (big_ring_pos > 0) {
	ring_pos = (big_ring_pos % MAX_ERROR_RING);
	/* Check that the indices match */
	if (DebugFlag) {
	    PRINTF( "Looking for ring[%d] with big ring %d\n", 
		    ring_pos, big_ring_pos );
	}
	if (error_ring_idx[ring_pos] == big_ring_pos) {
	    if (DebugFlag) {
		PRINTF( "Found error message in ring %d: %s\n", 
			ring_pos, error_ring[ring_pos] );
	    }
	    return error_ring[ring_pos];
	}
	else if (DebugFlag) {
	    PRINTF( "error_ring_idx[%d] = %d != big_ring_pos = %d\n",
		    ring_pos, error_ring_idx[ring_pos], big_ring_pos );
	}
    }
    else if (DebugFlag) {
	PRINTF( "Errcode %x has ring position 0\n", errcode );
    }
    return 0;
}

int MPIR_GetErrorMessage( int errcode, char *dummy, const char **errmsg )
{
    const char *msg;

    _CheckForDebug();

    if (DebugFlag) {
	PRINTF( "GetErrorMessage for code %d\n", errcode );
	_PrintErrCode( errcode );
    }
    /* Check for valid message */
    if (errcode && (errcode & MPIR_ERR_CLASS_MASK) == 0) {
	/* Bogus error code */
	if (DebugFlag) {
	    PRINTF( "Bogus error code %d (class is 0)\n", errcode );
	}
	/* Convert it to an "invalid error code" message */
	errcode = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ERRORCODE, 
				   (char *)0, (char *)0, (char *)0, errcode );
    }
    msg = MPIR_Err_map_code_to_string( errcode );
    if (!msg || !msg[0]) {
	/* pick up the default string */
	if (DebugFlag) {
	    PRINTF( "Map_code for %d returned null or blank\n", errcode );
	}
	msg = MPIR_Get_error_string( errcode );
    }
    *errmsg = msg;

    /* Old prototype has int return */
    return 0;
}
/*
 * The following code accesses the default error messages
 * These are maintained automatically by processing the source files with
 * the script GetMsgCat, which writes two files:
 * A message catalog, using sets for class and kind values in each set for the
 * index in set, and an include file containing the text of all of the 
 * messages.  This ensures that we can always generate an error message.
 * The include file looks like
 * static char *(kind0[]) = { "text0", "text1", ..., (char*)0 };
 * ...
 * static char **(errmsgs[max_class]) = { kind0, kind1, kind2, ... kindn };
 */

#include "mpierrstrings.h"

/* 
 * Adding error classes, codes, and strings:
 * These are constructed along the lines above:  The error codes and classes
 * are partitioned in the same was as the predefined values, except that 
 * we need to ensure that the classes/codes aren't in the "lastcode" range
 * (do we need to do this?)
 *
 * One way to handle this is to limit to range of available classes.
 * In each class, allow a list of codes.  The error_string call searches the
 * list as needed.
 */

const char *MPIR_Get_error_string( int errcode )
{
    int errclass, errkind, i;
    const char *msg, *tmsg;

    _CheckForDebug();
    
    errclass = errcode & MPIR_ERR_CLASS_MASK;
    /* errkind are 1 origin; a 0 is the same as 1 (but also indicates
       no kind specified, use the kind==1 value) */
    errkind  = (errcode & MPIR_ERR_CODE_MASK) >> MPIR_ERR_CLASS_BITS;

    if (DebugFlag) {
	PRINTF( "Get_error_string" );
	_PrintErrCode( errcode );
    }
    if (errclass < MPIR_MAX_ERRCLASS) {
	/* Make sure that errkind is defined */
	if (errkind == 0) errkind++;
	for (i=1; i<=errkind; i++) {
	    if (!errmsgs[errclass][i-1]) return 0;
	}
	msg = errmsgs[errclass][errkind-1];
	if (*msg == '\0' && errkind&0x1) {
	    tmsg = errmsgs[errclass][errkind];
	    if (tmsg && *tmsg) msg = tmsg;
	    if (DebugFlag) {
		PRINTF( "Message was null or blank, using  %s\n", msg );
	    }
	}
	if (DebugFlag) {
	    PRINTF( "Found message %s\n", msg );
	}
	return msg;
    }
    /* Otherwise, lookup user-defined string */
    /* ???? */
    return 0;
}

/*
 * This part of the file provides for error message handling including the use
 * of NLS message catalogs.
 */

#if defined(HAVE_CATOPEN) && defined(HAVE_NL_TYPES_H) && \
    defined(HAVE_GENCAT) 
#define USE_NLS_CAT
#endif

#if defined(USE_NLS_CAT)
#include <locale.h>
#include <nl_types.h>
static nl_catd errmsg = (nl_catd)(-1);
static int     opened_msgcat = 0;
static int     catavail = 0;
static int     usecat   = 1;

char *MPIR_GetNLSMsg( int errclass, int errkind, char *defmsg )
{
    char *msg = 0;
    char *lang;
    char *path;

    _CheckForDebug();

    if (DebugFlag) {
	PRINTF( "Looking in message catalog for messages\n" );
    }
    if (!opened_msgcat) {
	opened_msgcat = 1;
	
	/* Initialization */
#if defined(LC_MESSAGES)
	/* FreeBSD doesn't support LC_MESSAGES locale! */
	lang = getenv( "LANG" );
	if (!lang) lang = "C";
	if (DebugFlag) {
	    PRINTF( "setlocale( LC_MESSAGES, %s )\n", lang );
	}
	setlocale( LC_MESSAGES, lang );
#endif
	errmsg = catopen("mpich.cat",0);
	if (errmsg == (nl_catd)-1) {
	    /* Try MPICHNLSPATH */
	    path = getenv( "MPICHNLSPATH" );
	    if (path) {
		/* Only a single directory is allowed for now */
		char fullpath[1024];
		strncpy( fullpath, path, 1023 );
		strcat( fullpath, "/mpich.cat" );
		if (DebugFlag) {
		    PRINTF( "catopen( %s, 0 )\n", fullpath );
		}
		errmsg = catopen( fullpath, 0 );
		if (errmsg == (nl_catd)-1) {
		    strncpy( fullpath, path, 1023 );
		    strcat( fullpath, "/mpich.En_US.cat" );
		    if (DebugFlag) {
			PRINTF( "catopen( %s, 0 )\n", fullpath );
		    }
		    errmsg = catopen( fullpath, 0 );
		}
	    }
	    else {
		/* Try absolute location */
		if (DebugFlag) {
		    PRINTF( "catopen( %s, 0 )\n", "/home/MPI/mpich/lib/mpich.cat" );
		}
		errmsg = catopen( 
		    "/home/MPI/mpich/lib/rs6000/mpich.cat", 0 );
	    }
	}
	catavail = (errmsg != (nl_catd)-1) ;
    }

    /* Getting a message */
    if (usecat && catavail) {
	/* Args to catgets are nl_catd, setnum, msgnum, defaultmsg */
	msg = catgets( errmsg, errclass, errkind + 1, defmsg );
	if (DebugFlag) {
	    PRINTF( "catgets( , %d, %d, ) returned %s\n", 
		    errclass, errkind + 1, msg ? msg : "<NULL>" );
	}
    }
    else {
	msg = defmsg;
	if (DebugFlag) {
	    PRINTF( "Returning default message %s\n", msg );
	}
    }
    
    return msg;
}


/* Part of rundown */
/* catclose( errmsg ); */

#endif

#ifdef TEST_MSGS
/*
 * Test the message programs
 */
#include <stdio.h>

int main( int argc, char **argv )
{
    int i, j, ecode, rlen, rc;
    int errclass, errkind;
    char msg1[MPI_MAX_ERROR_STRING+1];
    char msg2[MPI_MAX_ERROR_STRING+1];
    char *newmsg;

    for (errclass=0; errclass<MPIR_MAX_ERRCLASS; errclass++) {
	for (errkind=0; errkind<60; errkind++) {
	    ecode = MPIR_ERRCLASS_TO_CODE(errclass,errkind);
#ifdef FOO
	    /* Turn off use of message catalog */
	    usecat = 0;
	    rc = MPI_Error_string( ecode, msg2, &rlen );
	    /* Re-enable message catalog */
	    usecat = 1;
	    if (rc) continue;
	    PRINTF( "%d(%x) %s\n", ecode, ecode, msg2 );
#endif
	    MPIR_GetErrorMessage( ecode, (char *)0, &newmsg );
	    if (newmsg)
		PRINTF( "%d:%d-%d(%x) %s\n", errclass, errkind, ecode, ecode, newmsg );
#ifdef FOO
	    else
		PRINTF( "%d:%d-%d(%x) <NULL>\n", errclass, errkind, ecode, ecode );
#endif
	}
    }
    return 0;
}

#endif

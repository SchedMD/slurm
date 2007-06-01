
#include "mpiimpl.h"
#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
#include <string.h>
#endif

/* prototype for getenv */
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern char *getenv();
#endif


/*
 * This file provides for error message handling including the use
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

char *MPIR_GetNLSMsg ( int, char * );

char *MPIR_GetNLSMsg( errnum, defmsg )
int errnum;
char *defmsg;
{
    char *msg = 0;
    char *lang;
    char *path;

    if (!opened_msgcat) {
	opened_msgcat = 1;
	
	/* Initialization */
#if defined(LC_MESSAGES)
	/* FreeBSD doesn't support LC_MESSAGES locale! */
	lang = getenv( "LANG" );
	if (!lang) lang = "C";
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
		errmsg = catopen( fullpath, 0 );
		if (errmsg == (nl_catd)-1) {
		    strncpy( fullpath, path, 1023 );
		    strcat( fullpath, "/mpich.en_US.cat" );
		    errmsg = catopen( fullpath, 0 );
		}
	    }
	    else {
		/* Try absolute location */
		errmsg = catopen( 
		    "/home/MPI/mpich/lib/rs6000/mpich.cat", 0 );
	    }
	}
	catavail = (errmsg != (nl_catd)-1) ;
    }

    /* Getting a message */
    if (usecat && catavail) {
	/* Args to catgets are nl_catd, setnum, msgnum, defaultmsg */
	int setnum = errnum & MPIR_ERR_CLASS_MASK;
	int errcod = (errnum & MPIR_ERR_CODE_MASK) >> MPIR_ERR_CLASS_BITS;
	msg = catgets( errmsg, setnum, errcod + 1, defmsg );
    }
    else
	msg = defmsg;
    
    return msg;
}


/* Part of rundown */
/* catclose( errmsg ); */

#endif

/* 
 * Independent of how we get the message, we'll want to process it for
 * values.
 *
 * When an error is set, a routine can place parameters (pointers) into the 
 * global variable
 *  MPIR_errargs[MPIR_errargcnt++];
 * 
 */

/* #define MPIR_MAX_ARGS 10 */
/* Definition/extern values in include/mpi_error.h */
void *(MPIR_errargs[MPIR_MAX_ARGS]);
int    MPIR_errargcnt = 0;

int MPIR_GetErrorMessage( errnum, defmsg, msgstr )
int errnum;
char *defmsg;
char **msgstr;
{
    char *msg;
    static char msgbuf[MPI_MAX_ERROR_STRING];

    /* Get the msg string */
#if defined(USE_NLS_CAT)
    msg = MPIR_GetNLSMsg( errnum, defmsg );
#else
    msg = defmsg;
#endif
    /* If there are arguments, process them */
    if (MPIR_errargcnt == 0) {
	*msgstr = msg;
    }
    else {
	char *p_in, *p_out, c;
	int  islong;
	int  curarg = 0;
	void **args = MPIR_errargs;
	p_in = msg;
	p_out = msgbuf;
	*msgstr = msgbuf;
	/* Copy the message into msgbuf, looking for the next '%' */
	while (p_in && *p_in) {
	    if (*p_in == '%') {
		/* Get the next character and process.
		   We handle %[l]{dsx} */
		c = *++p_in;
		islong = 0;
		if (c == 'l') {
		    c = *++p_in;
		    islong = 1;
		}
		switch (c) {
		case '%': *p_out++ = c; 
		    break;
		case 's': strcpy( p_out, (char *)(args[curarg++]) ); 
		    while (*p_out) p_out++;
		    break;
		case 'x': sprintf( p_out, (islong)?"%lx":"%x", 
		(islong)?*(long*)(args[curarg]):*(int*)(args[curarg]) );
		curarg++; 
		    while (*p_out) p_out++;
		    break;
		case 'd': sprintf( p_out, (islong)?"%ld":"%d", 
		(islong)?*(long*)(args[curarg]):*(int*)(args[curarg]) );
		curarg++; 
		    while (*p_out) p_out++;
		    break;
		default:
		    /* Need to issue an error message */
		    *p_out++ = '%';
		    *p_out++ = c;
		}
		p_in++;
	    }
	    else {
		*p_out++ = *p_in++;
	    }
	}
	/* Make sure that all errargs are removed, even if we didn't 
	   need them */
	MPIR_errargcnt = 0;
	/* Add string terminator */
	*p_out = 0;
    }
    return 0;
}

#ifdef TEST_MSGS
/*
 * Test the message programs
 */
#include <stdio.h>

int main( argc, argv )
int argc;
char **argv;
{
    int i, j, ecode, rlen, rc;
    char msg1[MPI_MAX_ERROR_STRING];
    char msg2[MPI_MAX_ERROR_STRING];
    char *newmsg;

    for (i=0; i<=MPI_ERR_REQUEST; i++) {
	for (j=0; j<8; j++) {
	    ecode = i + (j << MPIR_ERR_CLASS_BITS);
	    /* Turn off use of message catalog */
	    usecat = 0;
	    rc = MPI_Error_string( ecode, msg2, &rlen );
	    /* Re-enable message catalog */
	    usecat = 1;
	    if (rc) continue;
	    printf( "%d(%x) %s\n", ecode, ecode, msg2 );

	    MPIR_GetErrorMessage( ecode, (char *)0, &newmsg );
	    if (newmsg)
		printf( "%d(%x) %s\n", ecode, ecode, newmsg );
	    else
		printf( "%d(%x) <NULL>\n", ecode, ecode );
	}
    }
    return 0;
}
#endif

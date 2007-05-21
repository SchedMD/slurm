#include "mpeconf.h"

#include "clogimpl.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#ifdef HAVE_WINDOWS_H
#include <io.h>
#endif
#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif

/* The header information for an alog file needs to be collected as the file
 * is read.  Therefore we collect it here in memory as we create the main part
 * of the alogfile, and write it out later.
*/

static CLOG_STATE statedefs[400];	/* state definitions */
static CLOG_EVENT eventdefs[1000];	/* event definitions */
static int currsdef      = 0;		/* next empty state definition */
static int curredef      = 0;		/* next empty event definition */
static int numevents	  = 0;		/* alog event count */
static int procsfound[256];		/* process ids found in logfile */
static int numprocs	  = 0;	        /* number of processes found so far */
static int typesfound[256];		/* event types found in logfile */
static int numtypes	  = 0;	        /* number of types found so far */
/* static int numtasks	  = 0;	*/	/* alog task field unused? */
static unsigned long firsttime = 0;	/* remember the first timestamp */
static unsigned long lasttime;		/* remember the last timestamp */

static int  clogfd;			/* intput clogfile */
static FILE *alogfile;			/* output alogfile */
static FILE *atmpfile;			/* temp file for first pass */

int  clog2alog ( char * );
void alog_dumpblock ( double *);
void alog_dumphdr ( void );
static void checkproc ( int );
static void checktype ( int );

int clog2alog( char *execfilename )
{
    int n;
    double buf[ CLOG_BLOCK_SIZE / sizeof( double ) ];
    char alogfilename[256];
    char clogfilename[256];
    char line[80];		/* line of alog file */

    strcpy(clogfilename, execfilename);
    strcat(clogfilename, ".clog");
    if ((clogfd = OPEN(clogfilename, O_RDONLY, 0)) == -1) {
	fprintf(stderr, "could not open clogfile %s for reading\n",
		clogfilename);
	return(-2);
    }

    if ((atmpfile = fopen("ctoatmp","w")) == NULL) {
	fprintf(stderr, "could not open ctoatmp for writing\n");
	return(-3);
    }

    n = read(clogfd, buf, CLOG_BLOCK_SIZE);
    while (n) {
	if (n != CLOG_BLOCK_SIZE) {
	    fprintf(stderr,"could not read %d bytes\n", CLOG_BLOCK_SIZE);
	    return(-3);
	}
	alog_dumpblock(buf);
	n = read(clogfd, buf, CLOG_BLOCK_SIZE);
    }	

    close(clogfd);
    fclose(atmpfile);

    strcpy(alogfilename, execfilename);
    strcat(alogfilename, ".alog");
    if ((alogfile = fopen(alogfilename,"w")) == NULL) {
	fprintf(stderr, "could not open alogfile %s for writing\n",
		alogfilename);
	return(-3);
    }
    alog_dumphdr();		/* write negative events */

    /* now copy tmp file onto end of alogfile */
    if ((atmpfile = fopen("ctoatmp","r")) == NULL) {
	fprintf(stderr, "could not reopen ctoatmp for reading\n");
	return(-3);
    }
    while (fgets(line, 80, atmpfile) != NULL) 
	fputs(line, alogfile);

    fclose(alogfile);
    unlink("ctoatmp");
    return(0);
}

void alog_dumphdr()
{
    int i;

    fprintf(alogfile,"-1 0 0 0 0 0 Me\n");
    fprintf(alogfile,"-2 0 0 %d 0 0\n", numevents);
    fprintf(alogfile,"-3 0 0 %d 0 0\n", numprocs);
    fprintf(alogfile,"-4 0 0 1 0 0\n");	/* tasks, unused */
    fprintf(alogfile,"-5 0 0 %d 0 0\n", numtypes);
    /* fprintf(alogfile,"-6 0 0 0 0 %lu\n", firsttime); */
    fprintf(alogfile,"-6 0 0 0 0 %lu\n", (unsigned long) 0); /*times shifted to start at 0 */
    fprintf(alogfile,"-7 0 0 0 0 %lu\n", lasttime);
    fprintf(alogfile,"-8 0 0 1 0 0\n");	/* timer cycles, unused */
    fprintf(alogfile,"-11 0 0 0 0 0\n");/* rollover */

    for (i = 0; i < curredef; i++) /* event definitions */
    {
	fprintf(alogfile,"-9 0 0 %d 0 0 %s\n",
		eventdefs[i].etype,
		eventdefs[i].description);
    }

    for (i = 0; i < currsdef; i++) /* state definitions */
    {
	fprintf(alogfile,"-13 0 %d %d 0 0 %s %s\n",
		statedefs[i].startetype,
		statedefs[i].endetype,
		statedefs[i].color,
		statedefs[i].description);
    }
}

void alog_dumpblock( p )
double *p;
{
    int         rtype;
    CLOG_HEADER *h;
    unsigned long alogtime;
    int procid;

    rtype = CLOG_UNDEF;
    while (rtype != CLOG_ENDBLOCK && rtype != CLOG_ENDLOG) {
	h	 = (CLOG_HEADER *) p;
#ifndef WORDS_BIGENDIAN
	adjust_CLOG_HEADER(h);
#endif
	rtype	 = h->rectype;
	procid	 = h->procid;
	if (h->timestamp == CLOG_MAXTIME) {
	    /* Unset time.  Change to zero? */
	    alogtime = 0;
	}
	else {
	    alogtime = (unsigned long) (1000000 * h->timestamp);
	    alogtime = alogtime - firsttime;        /* shift timestamps to start at 0 */
	}
	p	 = (double *) (h->rest);	/* skip to end of header */
	switch (rtype) {
	case CLOG_MSGEVENT:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_MSG ((CLOG_MSG *)p);
#endif
	    checkproc(procid);	/* check whether we have seen this proc */
	    checktype(((CLOG_MSG *) p)->etype); /*check whether we have seen this type*/
	    if (!numevents++) {
		firsttime = alogtime;
		alogtime = 0;
	    }
	    lasttime = alogtime;                /* save last timestamp */
	    fprintf(atmpfile, "%d %d %d %d %d %lu\n",
		    ((CLOG_MSG *) p)->etype, procid, 0, 0, 0, alogtime);
	    p = (double *) (((CLOG_MSG *) p)->end);
	    break;
	case CLOG_COLLEVENT:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_COLL ((CLOG_COLL *)p);
#endif
	    checkproc(procid);	                /* check if we have seen this proc */
	    checktype(((CLOG_COLL *) p)->etype);/* check if we have seen this type*/
	    if (!numevents++) {	                /* first event */
		firsttime = alogtime;
		alogtime = 0;
	    }
	    lasttime = alogtime;	        /* save last timestamp */
	    fprintf(atmpfile, "%d %d %d %d %d %lu\n",
		    ((CLOG_COLL *) p)->etype, procid, 0, 0, 0, alogtime);
	    p = (double *) (((CLOG_COLL *) p)->end);
	    break;
	case CLOG_RAWEVENT:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_RAW ((CLOG_RAW *)p);
#endif
	    checkproc(procid);	                /* check if we have seen this proc */
	    checktype(((CLOG_RAW *) p)->etype); /* check if we have seen this type*/
	    if (!numevents++) {	                /* first event */
		firsttime = alogtime;
		alogtime = 0;
	    }
	    lasttime = alogtime;	        /* save last timestamp */
	    fprintf(atmpfile, "%d %d %d %d %d %lu %s\n",
		    ((CLOG_RAW *) p)->etype, procid, 0,
		    ((CLOG_RAW *) p)->data, 0, alogtime,
		    ((CLOG_RAW *) p)->string );
	    p = (double *) (((CLOG_RAW *) p)->end);
	    break;
	case CLOG_SRCLOC:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_SRC ((CLOG_SRC *)p);
#endif
	    p = (double *) (((CLOG_SRC *) p)->end);
	    break;
	case CLOG_COMMEVENT:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_COMM ((CLOG_COMM *)p);
#endif
	    p = (double *) (((CLOG_COMM *) p)->end);
	    break;
	case CLOG_STATEDEF:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_STATE ((CLOG_STATE *)p);
#endif
	    statedefs[currsdef] = *((CLOG_STATE *) p);
	    currsdef++;
	    p = (double *) (((CLOG_STATE *) p)->end);
	    break;
	case CLOG_EVENTDEF:
#ifndef WORDS_BIGENDIAN
	    adjust_CLOG_EVENT ((CLOG_EVENT *)p);
#endif
	    eventdefs[curredef] = *((CLOG_EVENT *) p);
	    curredef++;
	    p = (double *) (((CLOG_EVENT *) p)->end);
	    break;
	case CLOG_ENDBLOCK:
	    break;
	case CLOG_ENDLOG:
	    break;
	default:
	    printf("unrecognized record type\n");
	}
    }
}

static void checkproc( int procid )
{
    int i, found = 0;

    for (i = 0; i < numprocs; i++) {
	if (procid == procsfound[i]) {
	    found = 1;
	    break;
	}
    }
    if (!found)
	procsfound[numprocs++] = procid;
}

static void checktype( int typeid )
{
    int i, found = 0;

    for (i = 0; i < numtypes; i++) {
	if (typeid == typesfound[i]) {
	    found = 1;
	    break;
	}
    }
    if (!found)
	typesfound[numtypes++] = typeid;
}

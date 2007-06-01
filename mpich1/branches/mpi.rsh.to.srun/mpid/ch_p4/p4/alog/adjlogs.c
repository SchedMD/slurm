/*
 * adjustlogs.c:  Program to take multiple alog logfiles, extract events
 *                for synchronizing the clocks, and generating adjusted times.
 *                The files are replaced, allowing the use of other alog tools.
 *
 * -e n defines synchronization events
 * -a1 n -a2 m -b1 k define pair-exchange events used to compute clock offsets
 * (There are predefined values; these allow the user to define their own)
 *
 * Algorithm:
 *     Build a matrix of time events; solve it for the offset and skew for
 *     each clock.  For the first pass, this "matrix" will have just the 
 *     "synchronization" events.
 *
 * This is the formula:
 * Processor 0 has the standard clock.
 * At the end of each sync, the clock are re-synchronized.
 * Thus, the global time for processor p is
 *    Find the interval I in synctime that contains the local time
 *    The adjusted gtime is:
 *
 *            stime[0][I+1]-stime[0][I]
 *    gtime = ------------------------- (time - stime[p][I]) + stime[0][I]
 *            stime[p][I+1]-stime[p][I]
 * 
 * The current implementation uses a single interval.
 *
 * Just to keep things more interesting, the timer is really a 64 bit clock,
 * with the field "time_slot" containing the high bits.
 *
 */
#include <stdio.h>
#include <ctype.h>           /* for calling isdigit()             */
#include "alog_evntdfs.h"       /* Logfile definitions */

#define FALSE 0
#define TRUE !FALSE
#define MAX(x,y)	( (x) > (y) ? (x) : (y) )

#define C_DATA_LEN 50

#define DO_NEGATIVE 1
#define IGNORE_NEGATIVE 2

struct log_entry
{
	int proc_id;
	int task_id;
	int event;
	int i_data;
	char c_data[C_DATA_LEN];
	int time_slot;
	unsigned long time;
};

#define MAX_NSYNC 2
typedef struct {
    unsigned long *time;            /* time values that were recorded */
    } SyncTime;
SyncTime synctime[MAX_NSYNC];

/* For now, we just handle a set of timing events (np-1 of them)
   between processor i and i+1 (processor 0 participates in only
   1 event) */
typedef struct {
    unsigned long a1, b1, a2;         /* Times for the events */
    int           p0, p1;             /* processors that participated in
					 this time-exchange */
    } OffsetEvents;
OffsetEvents *offsetevents;
int noffsetevents = 0;

/* A local offset is used to compensate for the time_slot (upper 32 bits) */
/* NOT YET IMPLEMENTED */
long local_offset;

/* The global time is found by adding an offset and scaling by
   a fraction that is represented by numer[i]/denom[i] on the i'th
   processor */
unsigned long *numer;
unsigned long *denom;
long          *globaloffset;

/* mintime holds the mintime for ALL runs; this can be used to 
   offset the values */
unsigned long mintime;

/* These hold user-defined synchronization events */
#define MAX_USERETYPES 100
static int syncevent[MAX_USERETYPES];
static int syncep=0;

/* These hold the 3 event types used to adjust the individual offsets
   (if not present, the synchronization events are used to compute the
   offsets)
 */
static int a1event[MAX_USERETYPES],
           a2event[MAX_USERETYPES],
           b1event[MAX_USERETYPES];
static int a1p = 0, a2p = 0, b1p = 0;

static unsigned long lowmask = 0xFFFF;

void ComputeOffsets();
           
main(argc,argv)
int argc;
char *argv[];
{
	FILE *headerfp, *fd;
	int  np, i, nsync, nlsync;
	char headerfile[255];
	int pid;
        int firstfile;

	if ( argc <= 1 ) 
		usage( argv[0] );

	/* Look for user-defined events */
	for (i=1; i<argc; i++) {
	    if (strcmp(argv[i],"-e") == 0) 
		/* Test on MAX_USERTYPES */
		syncevent[syncep++] = atoi(argv[++i]);
	    else if (strcmp(argv[i],"-a1") == 0)
	    	a1event[a1p++] = atoi(argv[++i]);
	    else if (strcmp(argv[i],"-a2") == 0) 
	    	a2event[a2p++] = atoi(argv[++i]);
	    else if (strcmp(argv[i],"-b1") == 0) 
	    	b1event[b1p++] = atoi(argv[++i]);
	    else
		break;
	    }
	/* Figure out how many processors there are */
	np        = argc - i;
	firstfile = i;
	/* These could be allocated on demand */
	for (i=0; i<MAX_NSYNC; i++) {
	    synctime[i].time       = (unsigned long *)
		                malloc( np * sizeof(unsigned long) );
	    }
	    
	globaloffset = (long *) malloc( np * sizeof(long) );
	numer        = (unsigned long *) malloc( np * sizeof(unsigned long) );
	denom        = (unsigned long *) malloc( np * sizeof(unsigned long) );
	offsetevents = (OffsetEvents *) malloc( np * sizeof(OffsetEvents) );
	mintime      = (unsigned long)(~0);

	/* Loop through each file, looking for the synchronization events */
	for (i=0; i<np; i++) {
	    fd = fopen( argv[firstfile+i], "r" );
	    nsync = extract_timing( i, fd );
	    if (i > 0 && nsync != nlsync) {
		fprintf( stderr, "Found differing numbers of syncs\n" );
		exit(0);
		}
	    nlsync = nsync;
	    fclose( fd );
	    }
	/* If we didn't find enough events, we exit */
	if (nsync < 2) {
	    fprintf( stderr, 
		     "Not enough synchronization events to adjust logs\n" );
	    exit(0);
	    }

	/* Compute a "global clock" time */
	/* NOTE: if numer is changed, ComputeOffsets must be changed as well */
	for (i=0; i<np; i++) {
	    numer[i]        = (synctime[1].time[0] - synctime[0].time[0]);
	    denom[i]        = (synctime[1].time[i] - synctime[0].time[i]);
	    /* Using mintime here fails for some log files (since some of the
	       computed/scaled times can then be negative.  We have to pick
	       a value that makes the minimum COMPUTED time positive */
	    globaloffset[i] = synctime[0].time[i]; /*   - mintime; */
	    }
	fprintf( stderr, "Summary of clock transformations:\n" );
	if (noffsetevents == np - 1) {
	    /* Print out the initial globaloffsets */
	    fprintf( stderr, "Global offsets from sync events are:\n" );
	    for (i=0; i<np; i++) {
		fprintf( stderr, "%4d  %12ld\n", i, globaloffset[i] );
		}
	    }

	/* Use adjust events to compute a modified offset (if such events
	   are not present, the globaloffset values above will be used) */
	ComputeOffsets( np );

	/* Write a summary */
	for (i=0; i<np; i++) {
	    fprintf( stderr, "%4d  (t - %12ld) (%lu/%lu)\n", 
		     i, globaloffset[i], numer[i], denom[i] );
	    }
        
	/* Rewrite the log files using the clock adjustment */
	for (i=0; i<np; i++) {
	    pid = getpid();
	    sprintf( headerfile, "%s.new", argv[firstfile+i] );
/* 	    sprintf(headerfile,"log.header.%d",pid); */
	    if ( (headerfp=fopen(headerfile,"w")) == NULL ) {
		fprintf(stderr,"%s: unable to create temp file %s.\n",
			argv[0], headerfile );
		exit(0);
		}
	    fd = fopen( argv[firstfile+i], "r" );
	    if (!fd) {
		fprintf( stderr, "%s: Unable to open log file %s\n", 
			 argv[0], argv[firstfile+i] );
		exit(0);
		}
	    adjust_file( i, fd, headerfp, 0, nsync, argv[firstfile+i] );
	    fclose( fd );
	    fclose( headerfp );

	    /* move filename */
/* 	    unlink( argv[firstfile+i] );
	    link( headerfile, argv[firstfile+i] );
	    unlink( headerfile );  */
	    }
	
} /* main */

/*
   Extract timing data for the i'th log file 
 */
int extract_timing( i, fd )
int  i;
FILE *fd;
{
struct log_entry entry;
int    nsync = -1;

while (1) {
    read_logentry(fd,&entry,DO_NEGATIVE);
    if ( feof(fd) ) break;
    if (is_sync_event(entry.event)) {
	/* We do this so that we save the LAST sync event */
	if (nsync + 1 < MAX_NSYNC) nsync++;
	synctime[nsync].time[i] = entry.time;
	/* fprintf( stdout, "Event type %d at time %d on proc %d\n",
		 entry.event, entry.time, i ); */
	}
	/* For the offset events, the assumption is that each processor
	   (except for processor 0) is the ORIGINATOR of one offsetevent.
	   It MAY participate as the respondent (b1 event) for multiple
	   events, including having processor 0 respond to EVERYONE.
	   Finally, the (b1) processor has processor number SMALLER than
	   the (a1,a2) processor.  This makes the equations that need
	   to be solved for the offsets TRIANGULAR and easy.
	 */
    else if (is_a1_event(entry.event)) {
    	offsetevents[i].a1 = entry.time;
    	offsetevents[i].p0 = entry.i_data;
        }
    else if (is_a2_event(entry.event)) {
    	offsetevents[i].a2 = entry.time;
    	offsetevents[i].p0 = entry.i_data;
    	noffsetevents++;
        }
    else if (is_b1_event(entry.event)) {
    	if (entry.i_data < i) {
    	    fprintf( stderr,
	             "Improper offset event (originating processor %d\n", i );
    	    fprintf( stderr, "higher numbered than partner %d)\n", 
		     entry.i_data );
    	    exit(0);
    	    }
    	offsetevents[entry.i_data].b1 = entry.time;
    	offsetevents[entry.i_data].p1 = i;
        }
    else if (entry.event > 0) {
	if (mintime > entry.time) mintime = entry.time;
	}
    }
return nsync + 1;
}

adjust_file( p, fin, fout, leave_events, nsync, fname )
FILE *fin, *fout;
int  p, leave_events, nsync;
char *fname;
{
struct log_entry entry;
unsigned long GlobalTime(), gtime;
unsigned long lasttime;

/* lasttime is used to make sure that we don't mess up the log files without
   knowing it */
lasttime = 0; 
while (1) {
    read_logentry(fin,&entry,DO_NEGATIVE);
    if ( feof(fin) ) break;
    if (!leave_events && (entry.event == ALOG_EVENT_SYNC ||
			  entry.event == ALOG_EVENT_PAIR_A1 ||
			  entry.event == ALOG_EVENT_PAIR_A2 ||
			  entry.event == ALOG_EVENT_PAIR_B1)) continue;
    /* adjust to the global clock time */
    gtime = GlobalTime(entry.time,p,nsync);
    if (entry.event > 0) {
	if (gtime < lasttime) {
	    fprintf( stderr, "Error computing global times\n" );
	    fprintf( stderr, "Times are not properly sorted\n" );
	    fprintf( stderr, "Last time was %lu, current time is %lu\n", 
		     lasttime, gtime );
	    fprintf( stderr, "(original new time is %lu)\n", entry.time );
	    fprintf( stderr, "processing file %s\n", fname );
	    exit(0);
	    }
	else 
	    lasttime = gtime;
	}
    /* negative events are unchanged. */
    fprintf(fout,"%d %d %d %d %d %lu %s\n",entry.event,
	    entry.proc_id,entry.task_id,entry.i_data,
	    entry.time_slot, (entry.event >= 0) ? gtime : entry.time, 
	    entry.c_data);
    }
}

usage( a )
char *a;
{
	fprintf(stderr,"%s: %s infile1 infile2 ...\n",a,a);
	fprintf(stderr,"  updates files with synchronized clocks\n");

	exit(0);
}

read_logentry(fp,table,do_negs)
FILE *fp;
struct log_entry *table;
int do_negs;
{
	char buf[81];
	char *cp;

	int i;

	do
	{	
		fscanf(fp,"%d %d %d %d %d %lu",
			&(table->event),&(table->proc_id),&(table->task_id),
			&(table->i_data),&(table->time_slot),&(table->time));

		cp = table->c_data;

		i = 0;

		do
		{
			fscanf(fp,"%c",cp);
		}
		while ( *cp == ' ' || *cp == '\t' );

		i++;

		while ( *cp != '\n' && i < C_DATA_LEN )
		{
			fscanf(fp,"%c",++cp);

			i++;
		}

		*cp = '\0';

		/*
		if ( !feof(fp) && table->event == 0 )
			fprintf(stderr,"0 reading in.\n");
		*/
	}
	while( table->event < 0 && do_negs == IGNORE_NEGATIVE && !feof(fp) );
}

/* This routine allows use to define MANY sync events */
int is_sync_event( type )
int type;
{
int i;

if (type == ALOG_EVENT_SYNC) return 1;
for (i=0; i<syncep; i++) 
    if (type == syncevent[i]) return 1;
return 0;
}

int is_a1_event( type )
int type;
{
int i;

if (type == ALOG_EVENT_PAIR_A1) return 1;
for (i=0; i<a1p; i++) 
    if (type == a1event[i]) return 1;
return 0;
}

int is_a2_event( type )
int type;
{
int i;

if (type == ALOG_EVENT_PAIR_A2) return 1;
for (i=0; i<a2p; i++) 
    if (type == a2event[i]) return 1;
return 0;
}

int is_b1_event( type )
int type;
{
int i;

if (type == ALOG_EVENT_PAIR_B1) return 1;
for (i=0; i<b1p; i++) 
    if (type == b1event[i]) return 1;
return 0;
}

unsigned long GlobalTime( time, p, nsync )
unsigned long time;
int           p, nsync;
{
unsigned long gtime, stime1, stime2;
unsigned long frac;
unsigned long tdiff;
unsigned long ScaleLong();

/* Problem: since times are UNSIGNED, we have to be careful about how they
   are adjusted.  time - synctime may not be positive.  We make sure that
   all of the subexpressions are unsigned longs */
if (time >= globaloffset[p]) {
    tdiff = time - globaloffset[p];
    frac  = ScaleLong( numer[p], denom[p], tdiff );
    gtime = frac + globaloffset[0];
    }
else {
    tdiff = globaloffset[p] - time;
    frac  = ScaleLong( numer[p], denom[p], tdiff );
    if (frac > globaloffset[0]) printf( "Oops!\n" );
    gtime = globaloffset[0] - frac;
    }
return gtime;
}

/*
    This routine takes offset events and solves for the offsets.  The
    approach is:

    Let the global time be given by (local_time - offset)*scale ,
    with a different offset and scale on each processor.  Each processor
    originates exactly one communication event (except processor 0),
    generating an a1 and a2 event.  A corresponding number of b2 events
    are generated, but note that one processor may have more than 1 b2
    event (if using Dunnigan's synchronization, there will be np-1 b2 events
    on processor 0, and none anywhere else).

    These events are:

   pi   a1 (send to nbr)                        (recv) a2
   pj                     (recv) b1 (send back)

    We base the analysis on the assumption that in the GLOBAL time
    repreresentation, a2-a1 is twice the time to do a (send) and
    a (recv).  This is equivalent to assuming that global((a1+a2)/2) ==
    global(b1).  Then, with the unknowns the offsets (the scales
    are assumed known from the syncevent calculation), the matrix is

    1
    -s0 s1
       ....
       -sj ... si

    where si is the scale for the i'th processor (note s0 = 1).
    The right hand sides are (1/2)(a1(i)+a2(i)) *s(i) - b1(j)*s(j).
    Because of the triangular nature of the matrix, this reduces to

       o(i) = (a1(i)+a2(i))/2 - (s(j)/s(i)) * (b1(j)-o(j))

    Note that if s(i)==s(j) and b1 == (a1+a2)/2, this gives o(i)==o(j).
    
 */
void ComputeOffsets( np )
int np;
{
int i, j;
unsigned long d1, delta;
unsigned long ScaleLong();

/* If there aren't enough events, return */
if (noffsetevents != np - 1) {
    if (noffsetevents != 0) 
	fprintf( stderr, 
	   "Incorrect number of offset events to compute clock offsets\n" );
    else
	fprintf( stderr, "No clock offset events\n" );
    return;
    }

/* Take globaloffset[0] from sync */
for (i=1; i<np; i++) {
    /* o(i) = (a1(i)+a2(i))/2 - (s(j)/s(i)) * (b1(j)-o(j)) */
    j  = offsetevents[i].p1;
    
    /* Compute a1(i)+a2(i)/2.  Do this by adding half the difference;
       this insures that we avoid overflow */
    d1 = (offsetevents[i].a2 - offsetevents[i].a1)/2;
    d1 = offsetevents[i].a1 + d1;

    /* We form (b1-o(j))(s(j)/s(i)) by noting that
       s(j)/s(i) == denom(i)/denom(j) (since numer(i)==numer(j)) */
    delta = ScaleLong( denom[i], denom[j],
                       offsetevents[i].b1 - globaloffset[j] );

    globaloffset[i] = d1 - delta;
    }
}

#include <mp.h>
static MINT *prod, *qq, *rr;
static int  mpallocated = 0;

unsigned long ScaleLong( n, d, v )
unsigned long n, d, v;
{
char buf[40];
char *s;
MINT *nn, *dd, *vv;
unsigned long q, r;

if (!mpallocated) {
    prod = itom(0);
    if (!prod) {
	fprintf( stderr, "Could not allocate mp int\n" );
	exit(0);
	}
    qq   = itom(0);
    if (!qq) {
	fprintf( stderr, "Could not allocate mp int\n" );
	exit(0);
	}
    rr   = itom(0);
    if (!rr) {
	fprintf( stderr, "Could not allocate mp int\n" );
	exit(0);
	}
    mpallocated = 1;
    }

sprintf( buf, "%x", n );
nn = xtom(buf);
if (!nn) {
    fprintf( stderr, "Could not allocate mp int\n" );
    exit(0);
    }
sprintf( buf, "%x", v );
vv = xtom(buf);
if (!vv) {
    fprintf( stderr, "Could not allocate mp int\n" );
    exit(0);
    }
sprintf( buf, "%x", d );
dd = xtom(buf);
if (!dd) {
    fprintf( stderr, "Could not allocate mp int\n" );
    exit(0);
    }

mult(nn,vv,prod);
mdiv(prod,dd,qq,rr);
s = mtox(qq);
sscanf( s, "%x", &q );
free( s );
s = mtox(rr);
sscanf( s, "%x", &r );
free( s );

/* Free the locals */
mfree( nn );
mfree( dd );
mfree( vv );

return q;
}


/* Here is not-quite working code for multiple precision arithmetic */
#ifdef DO_MULTIPLE_ARITH

/* 
   This routine takes a value v and scales it by (n/d).  This 
   routine handles integer overflow by using the following formulas:
   Let h(u) = high 16 bits of u, and l(u) = low 16 bits of u.
   Then v *(n/d) = 

   (l(v)+h(v))*(l(u)+h(u))/d 
   = l(v)l(n)/d + (l(n)h(v)+l(v)h(n))/d + h(v)h(n)/d
   
   == a1/d + a2/d + a3/d

   In order to keep the values in-range, we define low(u)=l(u) and
   high(u) = h(u) >> 16.  Then this formula becomes (with high substituted
   for h):

   a1/d + (a2<<16)/d + (a3<<32)/d

   Now, when doing the integer division, we need to propagate the remainders.
   Let the result be r.  Then

   rd = a1 + (a2<<16) + (a3<<32)

   if a1 = k1 d + b1, (a2 << 16) = (k2 d + b2), and (a3 << 32) = (k3 d + b3),
   then
   
   r d = (k1 d + b1) + (k2 d + b2) + (k3 d + b3);
       = (k1 + k2 + k3) d + (b1 + b2 + b3)
       
   and so

   r   = (k1 + k2 + k3) + (b1 + b2 + b3) / d

   To compute (k2,b2) and (k3,b3), we do:

   (a2<<16)/d:

   a2 = p2 d + c2
   a2 << 16 = p2 d << 16 + c2 << 16
            = (p2 << 16) d + c2 << 16
   Let c2 << 16 = r2 d + s2, then (finally!)
   a2 << 16 = (p2 << 16 + r2) d + s2

   (a3 << 32)/d:

   a3 = p3 d + c3
   a3 << 32 = p3 d << 32 + c3 << 32
            = (p3 << 32) d + c3 << 32
   Computing c3 << 32 = r3 d + s3 is a challange, particularly
   since we need only the low 32 bits (the high 32 bits will be 0)
   We do this in stages as well:

   c3 << 32 = (c3 << 16) << 16; 
   (c3 << 16) = t3 d + u3
   (c3 << 32) = (t3 << 16)d + u3 << 16
              = (t3 << 16 + y3)d + z3,
	      == r3 d + s3
   where u3 << 16 = y3 d + z3

   Then
   a3 << 32 = (p3 << 32 + r3) d + s3
 */
void DivLong();

/* 
   ScaleDecomp - convert (a << p) = alpha d + beta, with beta < d

   This works by recursively:
   a = b d + r,
   a<<p = (b << p)d + (r<<p)
   then process r<<p to bd + r' etc until b == 0
 */
void ScaleDecomp( a, p, d, alpha, beta )
int           p;
unsigned long a, d, *alpha, *beta;
{
unsigned long b, r;
unsigned long Alpha, Beta;
int      p1;

Alpha = 0; Beta = 0;

b     = a / d;
r     = a % d;
Alpha = b << p;
/* We need to gingerly deal with r, since shifting it by much
   may make it too large, particularly if d is nearly 32 bits.  
   What we need is r << p = gamma d + delta, with r < d.  This
   is really the hard part. We can not assume that d is much
   smaller than 32 bits, so this is tricky. */

DivLong( r, d, (unsigned long)(1 << p), &b, &r );
Alpha += b;
*beta = r;
#ifdef FOO
while (p > 1 && r > 0) {
    p1    = p/2;
    r     = (r << p1);
    b     = r / d;
    r     = r % d;
    Alpha += b << (p-p1);
    p      = (p - p1);
    }
*alpha = Alpha;
*beta  = r << p;
#endif
}
#define LOWBITS(a) (unsigned long)((a)&lowmask)
#define HIGHBITS(a) (unsigned long)( ((a) >> 16 ) & lowmask )

#include <mp.h>
unsigned long ScaleLong( n, d, v )
unsigned long n, d, v;
{
#ifdef FOO
#define LOWBITS(a) (unsigned long)((a)&lowmask)
#define HIGHBITS(a) (unsigned long)( ((a) >> 16 ) & lowmask )
unsigned long a1, a21, a22, a3, k1, k21, k22, k3, b1, b21, b22, b3;

DivLong( n, d, v, &k1, &b1 );
return k1 + b1/d;

a1  = LOWBITS(v)*LOWBITS(n);
a21 = LOWBITS(v)*HIGHBITS(n);
a22 = LOWBITS(n)*HIGHBITS(v);
a3  = HIGHBITS(v)*HIGHBITS(n);

k1 = a1 / d;
b1 = a1 % d;

ScaleDecomp( a21, 16, d, &k21, &b21 );
ScaleDecomp( a22, 16, d, &k22, &b22 );
ScaleDecomp( a3,  32, d, &k3,  &b3 );

return (k1 + k21 + k22 + k3) + (b1 + b21 + b22 + b3) / d;
#else
char buf[40];
MINT *nn, *dd, *vv, *prod, *qq, *rr;
unsigned long q, r;

sprintf( buf, "%x", n );
nn = xtom(buf);
sprintf( buf, "%x", v );
vv = xtom(buf);
sprintf( buf, "%x", d );
dd = xtom(buf);
prod = itom(0);
qq   = itom(0);
rr   = itom(0);
mult(nn,vv,prod);
mdiv(prod,dd,qq,rr);
sscanf( mtox(qq), "%x", &q );
sscanf( mtox(rr), "%x", &r );
return q;
#endif
}

/*
  Represent nv = alpha d + beta
 */
void DivLong( n, d, v, alpha, beta )
unsigned long n, d, v;
unsigned long *alpha, *beta;
{
unsigned long a1, a21, a22, a3, k1, k21, k22, k3, b1, b21, b22, b3;

a1  = LOWBITS(v)*LOWBITS(n);
a21 = LOWBITS(v)*HIGHBITS(n);
a22 = LOWBITS(n)*HIGHBITS(v);
a3  = HIGHBITS(v)*HIGHBITS(n);

k1 = a1 / d;
b1 = a1 % d;

ScaleDecomp( a21, 16, d, &k21, &b21 );
ScaleDecomp( a22, 16, d, &k22, &b22 );
ScaleDecomp( a3,  32, d, &k3,  &b3 );

*alpha = k1 + k21 + k22 + k3;
*beta  = b1 + b21 + b22 + b3;
}

#endif

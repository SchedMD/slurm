#include <stdio.h>

#include "mpi.h"
extern int __NUMNODES, __MYPROCID;
#include "mpptest.h"
#include "getopts.h"
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

/* 
    This file contains routines to generate output from the mpptest programs
 */

/* 
   In order to simplify the generation of graphs of multiple data sets, 
   we want to allow the generated cit code to contain the necessary 
   window selection commands.  To do this, we add arguments for
   -wx i n    windows in x, my # and total #
   -wy i n    windows in y, my # and total #
   -lastwindow generate the wait/new page.
 */
typedef enum { GRF_X, GRF_EPS, GRF_PS, GRF_GIF } OutputForm;

struct _GraphData {
    FILE *fp, *fpdata;
    char *fname2;
    void (*header)( GraphData, char *, char *, char *);
    void (*dataout)( GraphData, int, int, int, int, double, double, double, 
		     double, double );
    void (*draw)( GraphData, int, int, double, double );
    void (*endpage)( GraphData );
    /* For collective operations */
    void (*headergop)( GraphData, char *, char *, char * );    
    void (*dataoutgop)( GraphData, int, double, double, double, double, 
			double );
    void (*drawgop)( GraphData, int, int, double, double, int, int *);
    void (*endgraph)( GraphData );
    /* Information about the graph */
    int wxi, wxn, wyi, wyn, is_lastwindow;
    int givedy;
    int do_rate;
    int do_fit;
    int is_log;
    char *title;
    OutputForm output_type;
    };

/* Forward refs */
void HeaderCIt( GraphData ctx, char *protocol_name, char *title_string, 
		char *units );
void HeaderForGopCIt( GraphData ctx, char *test_name, char *title_string, 
		      char *units );
void DrawCIt( GraphData ctx, int first, int last, double s, double r );
void DrawGopCIt( GraphData ctx, int first, int last, double s, double r, 
		 int nsizes, int *sizelist );
void EndPageCIt( GraphData ctx );

void HeaderGnuplot( GraphData ctx, char *protocol_name, 
		    char *title_string, char *units );
void HeaderForGopGnuplot( GraphData ctx, char *protocol_name, 
			  char *title_string, char *units );
void DrawGnuplot( GraphData ctx, int first, int last, double s, double r );
void DrawGopGnuplot( GraphData ctx, int first, int last, double s, 
		     double r, int nsizes, int *sizelist );
void EndPageGnuplot( GraphData ctx );

void ChangeToRate( GraphData, int );

void EndGraphCIt( GraphData );
void EndGraphGnuplot( GraphData );

void PrintGraphHelp( void )
{
fprintf( stderr, "\n\
Output\n\
  -cit         Generate data for CIt\n\
  -gnuplot     Generate data for GNUPLOT (default)\n\
  -gnuploteps  Generate data for GNUPLOT in Encapsulated Postscript\n\
  -gnuplotps   Generate data for GNUPLOT in Postscript\n\
  -givedy      Give the range of data measurements\n\
  -rate        Generate rate instead of time\n\
  -fname filename             (default is stdout)\n\
               (opened for append, not truncated)\n\
  -noinfo      Do not generate plotter command lines or rate estimate\n\
  -wx i n      windows in x, my # and total #\n\
  -wy i n      windows in y, my # and total #\n\
  -title string Use string as the title instead of the default title\n\
  -lastwindow  generate the wait/new page (always for 1 window)\n" );
}


void HeaderCIt( GraphData ctx, char *protocol_name, char *title_string, 
		char *units )
{
    char archname[20], hostname[256], date[30], *p;
    int dummy;

    if (!ctx) return;

    fprintf( ctx->fp, "set default\nset font variable\n" );
    fprintf( ctx->fp, "set curve window y 0.15 0.90\n" );
    if (ctx->wxn > 1 || ctx->wyn > 1) 
	fprintf( ctx->fp, "set window x %d %d y %d %d\n", 
		 ctx->wxi, ctx->wxn, ctx->wyi, ctx->wyn );
    if (ctx->givedy) {
	/*fprintf( ctx->fp, "set order d d d x y d d d\n" )*/;
    }
    else {
	if (ctx->do_rate) 
	    fputs( "set order d d d x d y\n", ctx->fp );
	else
	    fprintf( ctx->fp, "set order d d d x y d\n" );
    }
    if (ctx->is_log) 
	fprintf( ctx->fp, "set scale x log y log\n" );

    if (ctx->do_rate) 
	fprintf( ctx->fp, "title left 'Rate (MB/sec)', bottom 'Size %s',\n", 
		 units );
    else
	fprintf( ctx->fp, "title left 'time (us)', bottom 'Size %s',\n", units );

    strcpy(archname,"MPI" );
    MPI_Get_processor_name(hostname,&dummy);
/* Must remove ' from hostname */
    p = hostname;
    while (*p) {
	if (*p == '\'') *p = ' ';
	p++;
    }
    strcpy(date , "Not available" );
    if (ctx->title) {
	/* Eventually could replace %D with date, %H with hostname */
	fprintf( ctx->fp, "top '%s'\n", ctx->title );
    }
    else {
/* For systems without a date routine, just leave off the data */
	if (!date[0] || strcmp( "Not available", date ) == 0) {
	    fprintf( ctx->fp, 
		     "      top 'Comm Perf for %s (%s)',\n 'type = %s'\n", 
		     archname, hostname, protocol_name );
	}
	else {
	    fprintf( ctx->fp, 
		     "      top 'Comm Perf for %s (%s)',\n 'on %s',\n 'type = %s'\n", 
		     archname, hostname, date, protocol_name );
	}
    }
    fprintf( ctx->fp, "\n#p0\tp1\tdist\tlen\tave time (us)\trate\n");
    fflush( ctx->fp );
}

void HeaderForGopCIt( GraphData ctx, char *test_name, char *title_string, 
		      char *units )
{
    char archname[20], hostname[256], date[30], *p;
    int  dummy;

    if (!ctx) return;

    fprintf( ctx->fp, "set default\nset font variable\n" );
    fprintf( ctx->fp, "set curve window y 0.15 0.90\n" );
    if (ctx->wxn > 1 || ctx->wyn > 1) 
	fprintf( ctx->fp, "set window x %d %d y %d %d\n", 
		 ctx->wxi, ctx->wxn, ctx->wyi, ctx->wyn );
    fprintf( ctx->fp, "title left 'time (us)', bottom 'Processes',\n" );
    strcpy(archname,"MPI" );
    MPI_Get_processor_name(hostname,&dummy);
/* Must remove ' from hostname */
    p = hostname;
    while (*p) {
	if (*p == '\'') *p = ' ';
	p++;
    }
    strcpy(date , "Not available" );
    if (ctx->title) {
	/* Eventually could replace %D with date, %H with hostname */
	fprintf( ctx->fp, "top '%s'\n", ctx->title );
    }
    else {
/* For systems without a date routine, just leave off the data */
	if (!date[0] || strcmp( "Not available", date ) == 0) {
	    fprintf( ctx->fp, 
		     "      top 'Comm Perf for %s (%s)',\n 'type = %s'\n", 
		     archname, hostname, test_name );
	}
	else {
	    fprintf( ctx->fp, 
		     "      top 'Comm Perf for %s (%s)',\n 'on %s',\n 'type = %s'\n", 
		     archname, hostname, date, test_name );
	}
    }
    fprintf( ctx->fp, "\n#np time (us) for various sizes\n");
    fflush( ctx->fp );
}

/* Time in usec */
void DataoutGraph( GraphData ctx, int proc1, int proc2, int distance, 
		   int len, double t, double mean_time, double rate,
		   double tmean, double tmax )
{
    if (!ctx) return;

    if(ctx->givedy) 
	fprintf( ctx->fpdata, "%d\t%d\t%d\t%d\t%f\t%.2f\t%f\t%f\n",
		 proc1, proc2, distance, len, tmean * 1.0e6, rate, 
		 mean_time*1.0e6, tmax * 1.0e6 );
    else {
	/* Update to use e3 or e6 for rate */
	fprintf( ctx->fpdata, "%d\t%d\t%d\t%d\t%f\t",
		 proc1, proc2, distance, len, mean_time*1.0e6 );
	if (rate > 1.0e6) {
	    fprintf( ctx->fpdata, "%.3fe+6\n", rate * 1.0e-6 );
	}
	else if (rate > 1.0e3) {
	    fprintf( ctx->fpdata, "%.3fe+3\n", rate * 1.0e-3 );
	}
	else {
	    fprintf( ctx->fpdata, "%.2f\n", rate );
	}
    }
}

void DataoutGraphForGop( GraphData ctx, int len, double t, double mean_time, 
			 double rate, double tmean, double tmax )
{
    if (!ctx) return;

    fprintf( ctx->fpdata, "%f ", mean_time*1.0e6 );
    fflush( ctx->fpdata );
}

void DataendForGop( GraphData ctx )
{
    if (!ctx) return;

    fprintf( ctx->fpdata, "\n" );
}

void DatabeginForGop( GraphData ctx, int np )
{
    if (!ctx) return;

    fprintf( ctx->fpdata, "%d ", np );
}

void RateoutputGraph( GraphData ctx, double sumlen, double sumtime, 
		      double sumlentime, double sumlen2, double sumtime2, 
		      int ntest, double *S, double *R )
{
    double  s, r;
    double  variance = 0.0;

    if (!ctx) return;

    PIComputeRate( sumlen, sumtime, sumlentime, sumlen2, ntest, &s, &r );
    s = s * 0.5;
    r = r * 0.5;
/* Do we need to wait until AFTER the variance to scale s, r ? */
    if (ntest > 1) {
	variance = (1.0 / (ntest - 1.0)) * 
	    (sumtime2 - 2.0 * s * sumtime - 2.0 * r * sumlentime + 
	     ntest * s * s + 2.0 * r * sumlen + r * r * sumlen2 );
    }

    if (ctx->do_fit) {					
	fprintf( ctx->fp, "# Model complexity is (%e + n * %e)\n", s, r );
	fprintf( ctx->fp, "# startup = " );
	if (s > 1.0e-3)
	    fprintf( ctx->fp, "%.2f msec ", s * 1.0e3 );
	else
	    fprintf( ctx->fp, "%.2f usec ", s * 1.0e6 );
	fprintf( ctx->fp, "and transfer rate = " );
	if (r > 1.e-6)
	    fprintf( ctx->fp, "%.2f Kbytes/sec\n", 1.0e-3 / r );
	else
	    fprintf( ctx->fp, "%.2f Mbytes/sec\n", 1.0e-6 / r );
	if (ntest > 1) 
	    fprintf( ctx->fp, "# Variance in fit = %f (smaller is better)\n", 
		     variance );
    }
    *S = s;
    *R = r;
}

void DrawCIt( GraphData ctx, int first, int last, double s, double r )
{
    if (!ctx) return;

/* Convert to one-way performance */
    if (ctx->givedy) {
	fprintf( ctx->fp, "set order d d d x y d d d\n" );
	if (ctx->do_rate) 
	    fputs( "set change y 'x * 1.0e-6'\n", ctx->fp );
	fprintf( ctx->fp, "plot\n" );
	fprintf( ctx->fp, "set order d d d x d d y d\n" );
	if (ctx->do_rate) 
	    fputs( "set change y 'x * 1.0e-6'\n", ctx->fp );
	fprintf( ctx->fp, "join\n" );
	fprintf( ctx->fp, "set order d d d x d d d y\n" );
	if (ctx->do_rate) 
	    fputs( "set change y 'x * 1.0e-6'\n", ctx->fp );
	fprintf( ctx->fp, "join\n" );
    }
    else {
	if (ctx->do_rate) 
	    fputs( "set change y 'x * 1.0e-6'\n", ctx->fp );
	fprintf( ctx->fp, "plot square\njoin\n" );
    }
/* fit some times fails in Gnuplot; use the s and r parmeters instead */
/* fit '1'+'x'\njoin dots\n   */
    if (!ctx->do_rate && ctx->do_fit) {
	fprintf( ctx->fp, "set function x %d %d '%f+%f*x'\n", 
		 first, last, s*1.0e6, r*1.0e6 );
	fprintf( ctx->fp, "join dots\n" );
    }
}

void DrawGopCIt( GraphData ctx, int first, int last, double s, double r, 
		 int nsizes, int *sizelist )
{
    int i, j;

    if (!ctx) return;

/* Do this in reverse order to help keep the scales correct */
    fprintf( ctx->fp, "set limits ymin 0\n" );
    for (i=nsizes-1; i>=0; i--) {
	fprintf( ctx->fp, "set order x" );
	for (j=0; j<i; j++)
	    fprintf( ctx->fp, " d" );
	fprintf( ctx->fp, " y" );
	for (j=i+1; j<nsizes; j++) 
	    fprintf( ctx->fp, " d" );
	fprintf( ctx->fp, "\nplot square\njoin '%d'\n", sizelist[i] );
    }
}

/*
   Redisplay using rate instead of time (not used yet)
 */
void ChangeToRate( GraphData ctx, int n_particip )
{
    if (!ctx) return;

    fprintf( ctx->fp, "set order d d d x d d y\njoin\n" );
}

/*
   Generate an end-of-page
 */
void EndPageCIt( GraphData ctx )
{
    if (!ctx) return;

    if (ctx->is_lastwindow)
	fprintf( ctx->fp, "wait\nnew page\n" );
}

/*
    GNUplot output 
 */
void HeaderGnuplot( GraphData ctx, char *protocol_name, 
		    char *title_string, char *units )
{
    char archname[20], hostname[256], date[30], *p;
    int  dummy;

    if (!ctx) return;

    switch (ctx->output_type) {
    case GRF_EPS:
	fprintf( ctx->fp, "set terminal postscript eps\n" );
	break;
    case GRF_PS:
	fprintf( ctx->fp, "set terminal postscript\n" );
	break;
    case GRF_GIF:
	fprintf( ctx->fp, "set terminal gif\n" );
	break;
    case GRF_X:
	/* Default behavior */
	break;
    }
    fprintf( ctx->fp, "set xlabel \"Size %s\"\n", units );
    fprintf( ctx->fp, "set ylabel \"time (us)\"\n" );
    if (ctx->is_log) 
	fprintf( ctx->fp, "set logscale xy\n" );
    strcpy(archname,"MPI" );
    MPI_Get_processor_name(hostname,&dummy);
/* Must remove ' from hostname */
    p = hostname;
    while (*p) {
	if (*p == '\'') *p = ' ';
	p++;
    }
    strcpy(date , "Not available" );
    if (!date[0] || strcmp( "Not available", date ) == 0) {
	fprintf( ctx->fp, "set title \"Comm Perf for %s (%s) type %s\"\n", 
		 archname, hostname, protocol_name );
    }
    else {
	fprintf( ctx->fp, "set title \"Comm Perf for %s (%s) on %s type %s\"\n", 
		 archname, hostname, date, protocol_name );
    }
    fprintf( ctx->fpdata, "\n#p0\tp1\tdist\tlen\tave time (us)\trate\n");
    fflush( ctx->fp );
}

void HeaderForGopGnuplot( GraphData ctx, char *protocol_name, 
			  char *title_string, char *units )
{
    char archname[20], hostname[256], date[30], *p;
    int  dummy;

    if (!ctx) return;

    fprintf( ctx->fp, "set xlabel \"Processes\"\n" );
    fprintf( ctx->fp, "set ylabel \"time (us)\"\n" );

    strcpy(archname,"MPI" );
    MPI_Get_processor_name(hostname,&dummy);
/* Must remove ' from hostname */
    p = hostname;
    while (*p) {
	if (*p == '\'') *p = ' ';
	p++;
    }
    strcpy(date , "Not available" );
    if (!date[0] || strcmp( "Not available", date ) == 0) {
	fprintf( ctx->fp, "set title \"Comm Perf for %s (%s) type %s\"\n", 
		 archname, hostname, protocol_name );
    }
    else {
	fprintf( ctx->fp, 
		 "set title \"Comm Perf for %s (%s) on %s type %s\"\n", 
		 archname, hostname, date, protocol_name );
    }
    fprintf( ctx->fpdata, "\n#np time (us) for various sizes\n");
    fflush( ctx->fp );
}

void DrawGnuplot( GraphData ctx, int first, int last, double s, double r )
{
    if (!ctx) return;

    if (ctx->givedy) {
	fprintf( ctx->fp, 
		 "plot '%s' using 4:5:7:8 notitle with errorbars", 
		 ctx->fname2 );
    }
    else {
	fprintf( ctx->fp, "plot '%s' using 4:5 notitle with ", ctx->fname2 );

/* Where is the configure test for this? */    
#ifdef GNUVERSION_HAS_BOXES
	fprintf( ctx->fp, "boxes,\\\n\
'%s' using 4:7 with lines", ctx->fname2 );
#else
	fprintf( ctx->fp, "lines" );
#endif
    }
    if (r > 0.0) {
	fprintf( ctx->fp, ",\\\n%f+%f*x with dots\n", 
		 s*1.0e6, r*1.0e6  );
    }
    else {
	fprintf( ctx->fp, "\n" );
    }
}

void DrawGopGnuplot( GraphData ctx, int first, int last, double s, 
		     double r, int nsizes, int *sizelist )
{
    int i;

    if (!ctx) return;

    fprintf( ctx->fp, "plot " );
    for (i=0; i<nsizes; i++) {
#ifdef GNUVERSION_HAS_BOXES
	fprintf( ctx->fp, "'%s' using 1:%d title '%d' with boxes%s\n\
'%s' using 1:%d with lines,\\\n", ctx->fname2, i+2, sizelist[i], 
		 ctx->fname2, i+2, (i == nsizes-1) ? "" : ",\\" );
#else
	fprintf( ctx->fp, "'%s' using 1:%d title '%d' with lines%s\n", 
		 ctx->fname2, i+2, sizelist[i], (i == nsizes-1) ? "" : ",\\" );
#endif
    }
}

/*
   Generate an end-of-page
 */
void EndPageGnuplot( GraphData ctx )
{
    if (!ctx) return;

    if (ctx->is_lastwindow) {
	if (ctx->output_type == GRF_X) 
	    fprintf( ctx->fp, 
		     "pause -1 \"Press <return> to continue\"\nclear\n" );
	else
	    fprintf( ctx->fp, "exit\n" );
    }
}

/* Common operations */
void HeaderGraph( GraphData ctx, char *protocol_name, char *title_string, 
		  char *units )
{
    if (!ctx) return;

    (*ctx->header)( ctx, protocol_name, title_string, units );
}

void HeaderForGopGraph( GraphData ctx, char *protocol_name, 
			char *title_string, char *units )
{
    if (!ctx) return;

    (*ctx->headergop)( ctx, protocol_name, title_string, units );
}

void DrawGraph( GraphData ctx, int first, int last, double s, double r )
{
    if (!ctx) return;

    (*ctx->draw)( ctx, first, last, s, r ) ;
}

void DrawGraphGop( GraphData ctx, int first, int last, double s, double r, 
		   int nsizes, int *sizelist )
{
    if (!ctx) return;

    (*ctx->drawgop)( ctx, first, last, s, r, nsizes, sizelist ) ;
}

void EndPageGraph( GraphData ctx )
{
    if (!ctx) return;

    (*ctx->endpage)( ctx );
}

#define MAX_TSTRING 1024

/* Common create */
GraphData SetupGraph( int *argc, char *argv[] )
{
    GraphData  new;
    char       filename[1024];
    int        wsize[2];
    int        isgnu;
    int        givedy;
    static char tstring[MAX_TSTRING];

    OutputForm output_type = GRF_X;

    new = (GraphData)malloc(sizeof(struct _GraphData));    if (!new)return 0;;

    filename[0] = 0;
    /* Set default.  The gnuplot isn't as nice (separate file for data) */
    /* But gnuplot is everywhere and C.It is available essentially nowhere */
    /* But on the other hand, the C.It output is human readable. So
       we still make that the default */
    isgnu = 0;
#ifdef FOO
#if GRAPHICS_PGM == gnuplot
    isgnu = 1;
#else    
    isgnu = 0;
#endif
#endif
    if (SYArgHasName( argc, argv, 1, "-gnuplot" )) isgnu = 1;
    if (SYArgHasName( argc, argv, 1, "-gnuploteps" )) {
	isgnu       = 1;
	output_type = GRF_EPS;
    };
    if (SYArgHasName( argc, argv, 1, "-gnuplotps" )) {
	isgnu       = 1;
	output_type = GRF_PS;
    };
    if (SYArgHasName( argc, argv, 1, "-gnuplotgif" )) {
	isgnu       = 1;
	output_type = GRF_GIF;
    };
    if (SYArgHasName( argc, argv, 1, "-cit" )) isgnu = 0;


    if (SYArgGetString( argc, argv, 1, "-fname", filename, 1024 ) &&
	__MYPROCID == 0) {
	new->fp = fopen( filename, "a" );
	if (!new->fp) {
	    fprintf( stderr, "Could not open file %s\n", filename );
	    return 0;
	}
    }
    else 
	new->fp = stdout;
    givedy = SYArgHasName( argc, argv, 1, "-givedy" );

    new->do_rate = SYArgHasName( argc, argv, 1, "-rate" );

    new->do_fit  = SYArgHasName( argc, argv, 1, "-fit" );

    if (SYArgGetString( argc, argv, 1, "-title", tstring, MAX_TSTRING ))
	new->title = tstring;
    else
	new->title = 0;

/* Graphics layout */
    new->wxi    = 1;
    new->wxn    = 1;
    new->wyi    = 1;
    new->wyn    = 1;
    new->givedy = givedy;
    if (SYArgGetIntVec( argc, argv, 1, "-wx", 2, wsize )) {
	new->wxi           = wsize[0];
	new->wxn           = wsize[1];
    }
    if (SYArgGetIntVec( argc, argv, 1, "-wy", 2, wsize )) {
	new->wyi           = wsize[0];
	new->wyn           = wsize[1];
    }
    new->is_lastwindow = SYArgHasName( argc, argv, 1, "-lastwindow" );
    if (new->wxn == 1 && new->wyn == 1) new->is_lastwindow = 1;

    /* Scaling type */
    new->is_log = 0;

    new->output_type = output_type;
    if (!isgnu) {
	new->header	= HeaderCIt;
	new->dataout    = DataoutGraph;
	new->headergop  = HeaderForGopCIt;
	new->dataoutgop = DataoutGraphForGop;
	new->draw	= DrawCIt;
	new->drawgop    = DrawGopCIt;
	new->endpage    = EndPageCIt;
	new->endgraph   = EndGraphCIt;
	new->fpdata	= new->fp;
	new->fname2	= 0;
    }
    else {
	char filename2[256];
	new->header	= HeaderGnuplot;
	new->dataout	= DataoutGraph;
	new->headergop	= HeaderForGopGnuplot;
	new->dataoutgop	= DataoutGraphForGop;
	new->draw	= DrawGnuplot;
	new->drawgop	= DrawGopGnuplot;
	new->endpage	= EndPageGnuplot;
	new->endgraph   = EndGraphGnuplot;
	if (filename[0]) {
	    /* Try to remove the extension, if any, from the filename */
	    char *p;
	    strcpy( filename2, filename );
	    p = filename2 + strlen(filename2) - 1;
	    while (p > filename2 && *p != '.') p--;
	    if (p > filename2)
		strcpy( p, ".gpl" );
	    else
		strcat( filename2, ".gpl" );
	}
	else {
	    strcpy( filename2, "mppout.gpl" );
	}
	if (__MYPROCID == 0) {
	    new->fpdata	    = fopen( filename2, "a" );
	    if (!new->fpdata) {
		fprintf( stderr, "Could not open file %s\n\
used for holding data for GNUPLOT\n", filename2 );
		return 0;
	    }
	}
	else 
	    new->fpdata   = 0;
	new->fname2 = (char *)malloc((unsigned)(strlen(filename2 ) + 1 ));
	strcpy( new->fname2, filename2 );
    }
    return new;
}

void DataScale( GraphData ctx, int isLog )
{
    if (!ctx) return;

    ctx->is_log = isLog;
}

void EndGraph( GraphData ctx )
{
    if (!ctx) return;

    (*ctx->endgraph)( ctx );
}

void EndGraphCIt( GraphData ctx )
{
    if (!ctx) return;

    if (ctx->fp && ctx->fp != stdout) fclose( ctx->fp );
}

void EndGraphGnuplot( GraphData ctx )
{
    if (!ctx) return;

    if (ctx->fpdata)                      fclose( ctx->fpdata );
    if (ctx->fp && ctx->fp != stdout)     fclose( ctx->fp );
}

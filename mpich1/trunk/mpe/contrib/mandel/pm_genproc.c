
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdio.h>
#include "mpi.h"
#include "mpe.h"
#include "pmandel.h"

#ifndef DEBUG
#define DEBUG 0
#endif
#define DEBUG_POINTS 0

/* I used the line:
gcc -P -E pm_genproc.c | sed 's/{[ ]*{/{@{/' | tr "@" "\n" | indent -st > pm_genproc_cleanedup.c
   to clean this up and see what it looked like.  Eek!
*/


/* hope this doesn't cause anybody problems */
double drand48();

#define DISP( a, b ) (int)((char *)(&(a)) - (char *)(&(b)))

void
FreeMPITypes()
{
  MPI_Type_free(&winspecs_type);
  MPI_Type_free(&flags_type);
  MPI_Type_free(&rect_type);
}

void 
DefineMPITypes()
{
  Winspecs winspecs;
  Flags flags;
  rect rectangle;
  MPI_Aint a, b;

  int len[4];
  MPI_Aint disp[4];
  MPI_Datatype types[4];

  NUM_type = MPI_DOUBLE;

  MPI_Type_contiguous( 8, MPI_INT, &winspecs_type );
  MPI_Type_commit( &winspecs_type );

  /* Skip the initial 4 pointers in flags, these should not
     be exchanged between processes.
   */
  len[0] = 12; /* 12 ints */
  len[1] = 2;  /* 2 doubles */
  len[2] = 6;  /* 6 NUM_types */

  MPI_Address( (void*)&flags.breakout, &a );
  MPI_Address( (void*)&flags, &b );
  disp[0] = a - b;
  MPI_Address( (void*)&flags.boundary_sq, &a );
  disp[1] = a - b;
  MPI_Address( (void*)&flags.rmin, &a );
  disp[2] = a - b;
  types[0] = MPI_INT;
  types[1] = MPI_DOUBLE;
  types[2] = NUM_type;
  MPI_Type_struct( 3, len, disp, types, &flags_type );
  MPI_Type_commit( &flags_type );

  len[0] = 5;
  MPI_Address( (void*)&rectangle.l, &a );
  MPI_Address( (void*)&rectangle, &b );
  disp[0] = a - b;
  types[0] = MPI_INT;
  MPI_Type_struct( 1, len, disp, types, &rect_type );
  MPI_Type_commit( &rect_type );

}


GetDefaultWinspecs( winspecs )
Winspecs *winspecs;
{
  winspecs->height = DEF_height;
  winspecs->width  = DEF_width;
  winspecs->bw     = DEF_bw;
  winspecs->xpos   = DEF_xpos;
  winspecs->ypos   = DEF_ypos;
  winspecs->numColors = DEF_numColors;
#if DEBUG
  fprintf( debug_file, "height = %d\nwidth = %d\nbw = %d\nxpos = %d\n",
	   winspecs->height, winspecs->width, winspecs->bw, winspecs->xpos );
  fprintf( debug_file, "ypos = %d\nnumColor = %d\n", winspecs->ypos,
	   winspecs->numColors );
  fflush( debug_file );
#endif
  return 0;
}

GetDefaultFlags( winspecs, flags )
Winspecs *winspecs;
Flags *flags;
{
  flags->logfile   = DEF_logfile;
  flags->inf       = DEF_inf;
  flags->outf      = DEF_outf;
  flags->winspecs  = winspecs;
  flags->breakout  = DEF_breakout;
  flags->randomize = DEF_randomize;
  flags->colReduceFactor = DEF_colReduceFactor;
  flags->loop      = DEF_loop;
  flags->zoom      = DEF_zoom;
  flags->askNeighbor = DEF_askNeighbor;
  flags->sendMasterComplexity = DEF_sendMasterComplexity;
  flags->drawBlockRegion = DEF_drawBlockRegion;
  flags->fractal   = DEF_fractal;
  flags->maxiter   = DEF_maxiter;
  flags->boundary_sq = DEF_boundary * DEF_boundary;
  flags->epsilon   = DEF_epsilon;
  NUM_ASSIGN( flags->rmin, DEF_rmin);
  NUM_ASSIGN( flags->rmax, DEF_rmax);
  NUM_ASSIGN( flags->imin, DEF_imin);
  NUM_ASSIGN( flags->imax, DEF_imax);
  NUM_ASSIGN( flags->julia_r, DEF_julia_r);
  NUM_ASSIGN( flags->julia_i, DEF_julia_i);

  flags->no_remote_X       = DEF_no_remote_X;
  flags->with_tracking_win = DEF_with_tracking_win;

#if DEBUG
  fprintf( debug_file, "logfile = %s\n", flags->logfile );
  fprintf( debug_file, "inf = %s\n", flags->inf );
  fprintf( debug_file, "outf = %s\n", flags->outf );
  fprintf( debug_file, "breakout = %d\n", flags->breakout );
  fprintf( debug_file, "randomize = %d\n", flags->randomize );
  fprintf( debug_file, "colReduceFactor = %d\n", flags->colReduceFactor );
  fprintf( debug_file, "loop = %d\n", flags->loop );
  fprintf( debug_file, "zoom = %d\n", flags->zoom );
  fprintf( debug_file, "askNeighbor = %d\n", flags->askNeighbor );
  fprintf( debug_file, "sendMasterComplexity = %d\n", flags->sendMasterComplexity);
  fprintf( debug_file, "drawBlockRegion = %d\n", flags->drawBlockRegion );
  fprintf( debug_file, "fractal = %d\n", flags->fractal );
  fprintf( debug_file, "maxiter = %d\n", flags->maxiter );
  fprintf( debug_file, "boundary_sq = %lf\n", flags->boundary_sq );
  fprintf( debug_file, "epsilon = %lf\n", flags->epsilon );
  fprintf( debug_file, "rmin = %lf\n", flags->rmin );
  fprintf( debug_file, "rmax = %lf\n", flags->rmax );
  fprintf( debug_file, "imin = %lf\n", flags->imin );
  fprintf( debug_file, "imax = %lf\n", flags->imax );
  fprintf( debug_file, "julia_r = %lf\n", flags->julia_r );
  fprintf( debug_file, "julia_i = %lf\n", flags->julia_i );
  fprintf( debug_file, "no_remote_X = %d\n", flags->no_remote_X );
  fprintf( debug_file, "with_tracking_win = %d\n", flags->with_tracking_win );
  fflush( debug_file );
#endif
  return 0;
}

int
GetWinspecs( argc, argv, winspecs )
int *argc;
char **argv;
Winspecs *winspecs;
{
  int myid;
  int numranks;

  MPI_Comm_rank( MPI_COMM_WORLD, &myid );
  MPI_Comm_size( MPI_COMM_WORLD, &numranks );
  
  if (!myid) {
    GetIntArg( argc, argv, "-height", &(winspecs->height) );
    GetIntArg( argc, argv, "-width",  &(winspecs->width) );
    winspecs->bw = IsArgPresent( argc, argv, "-bw" );
    GetIntArg( argc, argv, "-xpos", &(winspecs->xpos) );
    GetIntArg( argc, argv, "-ypos", &(winspecs->ypos) );
    GetIntArg( argc, argv, "-colors", &(winspecs->numColors) );
  }

#if DEBUG
  fprintf( debug_file, "[%d] before windspecs bcast\n", myid );
  fflush( debug_file );
#endif
  MPI_Bcast( winspecs, 1, winspecs_type, 0, MPI_COMM_WORLD );
#if DEBUG
  fprintf( debug_file, "[%d] after windspecs bcast\n", myid );
  fflush( debug_file );
#endif

  /* Each rank gets a color. Divide color range evenly.  */
  winspecs->my_tracking_color = (winspecs->numColors/numranks) * myid;

#if DEBUG
  fprintf( debug_file, "[%d]height = %d\n", myid, winspecs->height );
  fprintf( debug_file, "[%d]width = %d\n", myid, winspecs->width );
  fprintf( debug_file, "[%d]bw = %d\n", myid, winspecs->bw );
  fprintf( debug_file, "[%d]xpos = %d\n", myid, winspecs->xpos );
  fprintf( debug_file, "[%d]ypos = %d\n", myid, winspecs->ypos );
  fprintf( debug_file, "[%d]numColors = %d\n", myid, winspecs->numColors );
  fflush( debug_file );
#endif
  return 0;
}

GetFlags( argc, argv, winspecs, flags )
int *argc;
char **argv;
Winspecs *winspecs;
Flags *flags;
{
  double x, y;
  int myid, strLens[3];

  MPI_Comm_rank( MPI_COMM_WORLD, &myid );

  if (myid == 0) {
    GetStringArg( argc, argv, "-l", &(flags->logfile) );
    GetStringArg( argc, argv, "-i", &(flags->inf) );
      /* if reading from input file, disable zooming */
    if (flags->inf) {
      flags->zoom = 0;
    }
    GetStringArg( argc, argv, "-o", &(flags->outf) );
    GetIntArg( argc, argv, "-breakout", &(flags->breakout) );
    if (IsArgPresent( argc, argv, "-randomize" )) {
      flags->randomize = 0;
    }
    if (IsArgPresent( argc, argv, "+randomize" )) {
      flags->randomize = 1;
    }
    GetIntArg( argc, argv, "-colreduce", &(flags->colReduceFactor) );
    flags->loop = IsArgPresent( argc, argv, "-loop" );
    if (IsArgPresent( argc, argv, "-zoom" )) {
      flags->zoom = 0;
    }
    if (IsArgPresent( argc, argv, "+zoom" ) && !flags->inf ) {
      flags->zoom = 1;
    }
    flags->askNeighbor = IsArgPresent( argc, argv, "-neighbor" );
    flags->sendMasterComplexity = IsArgPresent( argc, argv, "-complexity" );
    flags->drawBlockRegion = IsArgPresent( argc, argv, "-delaydraw" );

    GetIntArg( argc, argv, "-with_tracking_win", &(flags->with_tracking_win) );
    GetIntArg( argc, argv, "-no_remote_X", &(flags->no_remote_X) );
    
    if (IsArgPresent( argc, argv, "-mandel" )) {
      flags->fractal = MBROT;
    } else if (IsArgPresent( argc, argv, "-julia" )) {
      flags->fractal = JULIA;
    } else if (IsArgPresent( argc, argv, "-newton" )) {
      flags->fractal = NEWTON;
    }
    
    GetIntArg( argc, argv, "-maxiter", &(flags->maxiter) );
    if (GetDoubleArg( argc, argv, "-boundary", &x )) {
      flags->boundary_sq = x * x;
    }
    GetDoubleArg( argc, argv, "-epsilon", &(flags->epsilon) );
    if (GetDoubleArg( argc, argv, "-rmin", &x )) {
      NUM_ASSIGN( flags->rmin, DBL2NUM( x ) );
    }
    if (GetDoubleArg( argc, argv, "-rmax", &x )) {
      NUM_ASSIGN( flags->rmax, DBL2NUM( x ) );
    }

    if (GetDoubleArg( argc, argv, "-imin", &x )) {
      NUM_ASSIGN( flags->imin, DBL2NUM( x ) );
    }
    if (GetDoubleArg( argc, argv, "-imax", &x )) {
      NUM_ASSIGN( flags->imax, DBL2NUM( x ) );
    }
    if (GetDoubleArg( argc, argv, "-radius", &x )) {
      if (GetDoubleArg( argc, argv, "-rcenter", &y )) {
	NUM_ASSIGN( flags->rmin, DBL2NUM( y-x ) );
	NUM_ASSIGN( flags->rmax, DBL2NUM( y+x ) );
      }
      if (GetDoubleArg( argc, argv, "-icenter", &y )) {
	NUM_ASSIGN( flags->imin, DBL2NUM( y-x ) );
	NUM_ASSIGN( flags->imax, DBL2NUM( y+x ) );
      }
    }
    strLens[0] = (flags->logfile) ? strlen(flags->logfile)+1 : 0;
    strLens[1] = (flags->inf)     ? strlen(flags->inf)+1     : 0;
    strLens[2] = (flags->outf)    ? strlen(flags->outf)+1    : 0;
  } /* End of myid == 0 */

#if DEBUG
     fprintf( stderr, "%d about to broadcast flags info\n", myid );
#endif
  MPI_Bcast( flags, 1, flags_type, 0, MPI_COMM_WORLD );

#if DEBUG
  fprintf (stderr, "%d about to broadcast string lengths\n", myid );
#endif
  MPI_Bcast( strLens, 3, MPI_INT, 0, MPI_COMM_WORLD );

  if (myid != 0) {

    if (flags->logfile != 0) {
      free(flags->logfile);
      flags->logfile = 0;
    }
    if (flags->inf != 0) {
      free(flags->inf);
      flags->inf = 0;
    }
    if (flags->outf != 0) {
      free(flags->outf);
      flags->outf = 0;
    }
    flags->logfile = (strLens[0]) ?
      (char *)malloc( strLens[0] * sizeof( char ) ) : 0;
    flags->inf = (strLens[1]) ?
      (char *)malloc( strLens[1] * sizeof( char ) ) : 0;
    flags->outf = (strLens[2]) ?
      (char *)malloc( strLens[2] * sizeof( char ) ) : 0;
  }
#if DEBUG
  fprintf( stderr, "%d about to broadcast strings\n", myid );
#endif
  if (strLens[0]) 
    MPI_Bcast( flags->logfile, strLens[0], MPI_CHAR, 0, MPI_COMM_WORLD );
  if (strLens[1]) 
    MPI_Bcast( flags->inf,     strLens[1], MPI_CHAR, 0, MPI_COMM_WORLD );
  if (strLens[2]) 
    MPI_Bcast( flags->outf,    strLens[2], MPI_CHAR, 0, MPI_COMM_WORLD );

#if DEBUG
  fprintf( debug_file, "[%d]logfile = %s\n", myid, flags->logfile );
  fprintf( debug_file, "[%d]inf = %s\n", myid, flags->inf );
  fprintf( debug_file, "[%d]outf = %s\n", myid, flags->outf );
  fprintf( debug_file, "[%d]breakout = %d\n", myid, flags->breakout );
  fprintf( debug_file, "[%d]randomize = %d\n", myid, flags->randomize );
  fprintf( debug_file, "[%d]colReduceFactor = %d\n", myid,
	   flags->colReduceFactor );
  fprintf( debug_file, "[%d]loop = %d\n", myid, flags->loop );
  fprintf( debug_file, "[%d]zoom = %d\n", myid, flags->zoom );
  fprintf( debug_file, "[%d]askNeighbor = %d\n", myid, flags->askNeighbor );
  fprintf( debug_file, "[%d]sendMasterComplexity = %d\n", myid,
	   flags->sendMasterComplexity );
  fprintf( debug_file, "[%d]drawBlockRegion = %d\n", myid,
	   flags->drawBlockRegion );
  fprintf( debug_file, "[%d]fractal = %d\n", myid, flags->fractal );
  fprintf( debug_file, "[%d]maxiter = %d\n", myid, flags->maxiter );
  fprintf( debug_file, "[%d]boundary_sq = %lf\n", myid, flags->boundary_sq );
  fprintf( debug_file, "[%d]epsilon = %lf\n", myid, flags->epsilon );
  fprintf( debug_file, "[%d]rmin = %lf\n", myid, flags->rmin );
  fprintf( debug_file, "[%d]rmax = %lf\n", myid, flags->rmax );
  fprintf( debug_file, "[%d]imin = %lf\n", myid, flags->imin );
  fprintf( debug_file, "[%d]imax = %lf\n", myid, flags->imax );
  fprintf( debug_file, "[%d]julia_r = %lf\n", myid, flags->julia_r );
  fprintf( debug_file, "[%d]julia_i = %lf\n", myid, flags->julia_i );
  fprintf( debug_file, "[%d]no_remote_X = %d\n", myid, flags->no_remote_X );
  fprintf( debug_file, "[%d]with_tracking_win = %d\n", myid, flags->with_tracking_win );
  fflush( debug_file );
#endif
  return 0;
}



Pixel2Complex( flags, x, y, nx, ny )
Flags *flags;
int x, y;
NUM *nx, *ny;
{
  NUM_PTR_ASSIGN(
    nx,
    NUM_ADD(
      NUM_MULT(
	DBL2NUM( (double)x / flags->winspecs->width ),
	NUM_SUB( flags->rmax, flags->rmin )),
      flags->rmin )
  );

  NUM_PTR_ASSIGN(
    ny,
    NUM_ADD(
      NUM_MULT(
        DBL2NUM( (double)y / flags->winspecs->height ),
        NUM_SUB( flags->imin, flags->imax )),
      flags->imax )
  );

/*
  fprintf( stderr, "In (%d %d) to (%lf,%lf)-(%lf,%lf)\n",
	   flags->winspecs->width, flags->winspecs->height,
	   flags->rmin, flags->imin, flags->rmax, flags->imax );
  fprintf( stderr, "Converted (%d, %d) to (%lf, %lf)\n",
	   x, y, *nx, *ny );
*/

  return 0;
}


StrContainsNonWhiteSpace( str )
char *str;
{
  while (*str) {
    if (!isspace( *str )) return 1;
    str++;
  }
  return 0;
}




/* Q_Create - create the queue */
void Q_Create( q, randomize )
rect_queue *q;
int randomize;
{
  q->head = q->tail = 0;       /* create the queue */
  q->size = 100;
  q->r = (rect *) malloc( q->size * sizeof( rect ) );
  q->randomPt = 1;
  q->randomize = randomize;
}

void Q_Destroy(q)
rect_queue *q;
{
  if ( (q != NULL) && (q->r != NULL) ) {
    free(q->r);
  }
}

/* Q_Checksize - check if the queue is full.  If so, double the size */
void Q_Checksize( q )
rect_queue *q;
{
  if (q->head == q->tail+1 ||
      !q->head && q->tail == q->size - 1) {
				/* if the queue is full */
    q->r = (rect *) realloc( q->r, sizeof( rect ) * q->size * 2 );
				/* get a bigger queue */
    if (q->tail < q->head) {
      memcpy( q->r + q->size, q->r, q->tail * sizeof( rect ) );
				/* copy over any data that needs to be moved */
      q->tail += q->size;
    }
    if (q->randomize && q->randomPt<q->head) {
      q->randomPt += q->size;
    }
    q->size *= 2;
  }
}

void
Q_Print( q )
rect_queue *q;
{
  int i;
  i = q->head;
  while (i!=q->tail) {
    fprintf( debug_file, "queue[%d] = (%d %d %d %d)\n", i, q->r[i].l, q->r[i].r,
	    q->r[i].t, q->r[i].b );
    i++;
    if (i==q->size) i = 0;
  }
}

int
Q_CheckValidity( q )
rect_queue *q;
{
  int i;
  i = q->head;
  while (i != q->tail) {
    if (q->r[i].l > 10000 ||
	q->r[i].r > 10000 ||
	q->r[i].t > 10000 ||
	q->r[i].b > 10000 ||
	q->r[i].length > 10000) {
      fprintf( debug_file, "Error in queue[%d]: (%d %d %d %d %d)\n",
	       i, q->r[i].l, q->r[i].r, q->r[i].t, q->r[i].b, q->r[i].length );
    }
    if (++i == q->size) i=0;
  }
  return 0;
}


/* Q_Enqueue - add a rectangle to the queue */
void Q_Enqueue( q, r )
rect_queue *q;
rect *r;
{
#if DEBUG
  Q_CheckValidity( q );
#endif
  Q_Checksize( q );
  q->r[q->tail] = *r;
#if DEBUG>1
  fprintf( debug_file, "added to queue at %d\n", q->tail );
#endif
  if (++q->tail == q->size) q->tail = 0;
#if DEBUG>2
  fprintf( debug_file, "Added to queue:\n" );
  Q_Print( q );
#endif
#if DEBUG
  Q_CheckValidity( q );
#endif
}

/* Q_Dequeue - remove a rectangle from the queue */
void Q_Dequeue( q, r )
rect_queue *q;
rect *r;
{
  double rand_no;
#if DEBUG
  Q_CheckValidity( q );
#endif
  *r = q->r[q->head];
#if DEBUG>1
  fprintf( debug_file, "remove from queue at %d\n", q->head );
#endif
  if (++q->head == q->size) q->head = 0;
  if (q->randomize && ((q->head == q->randomPt) ||
		    (q->head == q->randomPt + 1))) {
    int i, j, numItems;
    rect temp;
    numItems = (q->tail<q->head)
	? q->size-q->head + q->tail
	    : q->tail - q->head;
    for (i=q->head; i!=q->tail; i++) {
      rand_no = drand48();
      j = (int)(rand_no * numItems) + q->head;
      if (j>=q->size) j-=q->size;
      temp = q->r[j];
      q->r[j] = q->r[i];
      q->r[i] = temp;
      if (i==q->size-1) {
	i = -1;
      }
    }
    q->randomPt = q->tail;
  }
#if DEBUG>2
  fprintf( debug_file, "Removed from queue:\n" );
  Q_Print( q );
#endif
#if DEBUG
  Q_CheckValidity( q );
#endif
}




int RectBorderLen( r )
rect *r;
{
  return (r->r-r->l) ?
           (r->b-r->t) ?
	     (2 * (r->r-r->l+r->b-r->t) )
	   :
	     (r->r - r->l + 1)
	 :
	   (r->b-r->t) ?
	     (r->b - r->t + 1)
	   :
	     1;
}

void
PrintHelp( progName )
char *progName;
{
  printf( "Options recognized by %s:\n", progName );
  printf( "(defaults are in parentheses () )\n" );
/*
  printf( "   -o <filename>              (<stdout>) output file\n" );
*/
  printf( "   -i <filename>              (none) input file\n" );
#if LOG
  printf( "   -l <filename>              (\"%s\") name of log file\n", DEF_logfile );
#endif
  printf( "   -xpos <xpos>               (%d) window horizontal coordinate\n",
	  DEF_xpos );
 printf( "   -ypos <xpos>               (%d) window vertical coordinate\n",
	  DEF_ypos );
  printf( "   -width <width>             (%d) width of computed area in points\n", DEF_width );
  printf( "   -height <height>           (%d) height of computed area in points\n", DEF_height );
  printf( "   -boundary <boundary>       (%.1lf) boundary value for M-set computation\n", DEF_boundary );
  printf( "   -maxiter <max. iter>       (%d) maximum # of iterations for M-set\n", DEF_maxiter );
  printf( "                              compuptation algorithm\n" );
  printf( "   -rmin <real min.>          (%.2lf) minimum real coordinate of computed area\n", DEF_rmin );
  printf( "   -rmax <real max.>          (%.2lf) maximum real coordinate of computed area\n", DEF_rmax );
  printf( "   -imin <imag. min.>         (%.2lf) minimum imaginary coordinate of computed\n", DEF_imin );
  printf( "                              area\n" );
  printf( "   -imax <imag. max.>         (%.2lf) maximum imaginary coordinate of computed\n", DEF_imax );
  printf( "                              area\n" );
  printf( "\n" );
  printf( "      alternate form: (if specified, overrides <r|i><min|max>)\n" );
  printf( "   -rcenter <real center>     (%.2lf) center real coordinate of computed area\n", (DEF_rmin+DEF_rmax)/2 );
  printf( "   -icenter <imag. center>    (%.2lf) center imaginary coordinate of computed\n", (DEF_imin+DEF_imax)/2 );
  printf( "                              area\n" );
  printf( "   -radius <area radius>      (%.2lf) radius of the computed area\n", (DEF_rmax - DEF_rmin) );
  printf( "\n" );
  printf( "   -breakout <breakout size>  (%d) maximum length or width rectangle to\n", DEF_breakout );
  printf( "                              subdivide\n" );
  printf( "   -no_remote_X <0|1>         (%d) Boolean, if true (1) all X display is handled\n", DEF_no_remote_X );
  printf( "                                   is handled by rank 0.\n");
  printf( "   -with_tracking_win <0|1>   (%d) Boolean, if true (1) add a second output window\n", DEF_with_tracking_win );
  printf( "                                   showing who computed what part of the output.\n");
  printf( "   -tol <num pixels>          (2) Integer (mouse drag tolerence),\n"
          "                                  When using the mouse to zoom in on a picture,\n"
          "                                  dragging less than this number of pixels\n"
          "                                  will be interpreted as a simple click for\n" );
  printf( "                                  the purpose of quitting the program.\n" );
  printf( "   -colors <# of colors>      (%d) number of colors to request\n", DEF_numColors );
  printf( "   -colreduce <reduce factor> (%d) factor by which to scale down iteration\n", DEF_colReduceFactor );
  printf( "                              values to reduce color changes\n" );
  printf( "   <+,->zoom                  (%s) turn on (off) drag&zoom\n",
	  DEF_zoom ? "on" : "off" );
  printf( "   <+,->randomize             (%sset) (on,off) compute regions in as random of\n",
	 DEF_randomize ? "" : "not " );
  printf( "                              order as possible\n" );
/*
     printf( "   -tasksize <# of pixels>  (%d) approximate number of pixels to assign a slave\n", DEF_tasksize );
     printf( "        process after each request for work\n" );
*/
  printf( "   -bw                        (%sset) draw in black and white instead of\n", DEF_bw ? "" : "not " );
  printf( "                              color\n" );
  exit( 0 );
}


MPE_Color Iter2Color( flags, iter )
Flags *flags;
int iter;
{
  if (flags->winspecs->bw) {
    return ( (iter == flags->maxiter) ? MPE_BLACK :
	    ((iter / flags->colReduceFactor) % 2 ) ? MPE_WHITE : MPE_BLACK);
  } else {
    if (iter == flags->maxiter) {
      return MPE_BLACK;
    } else {
      return flags->winspecs->colorArray[ (iter / flags->colReduceFactor) %
			        flags->winspecs->numColors ];
    }
  }
}

void
ChunkIter2Color( flags, iterData, colorData, size )
Flags *flags;
int *iterData, size;
int *colorData;
{
  int i;

  for (i=0; i<size; i++) {
    *colorData = Iter2Color( flags, *iterData );
#if DEBUG>1
    fprintf( debug_file, "iter %d to color %d\n", *iterData, *colorData );
    fflush( debug_file );
#endif
    colorData++;
    iterData++;
  }
}




ComputeChunk( flags, r, pointData, iterData, maxnpoints, npoints )
Flags *flags;
rect *r;
int *iterData, maxnpoints, *npoints;
MPE_Point *pointData;
{
  int i, x, y;
#if DEBUG
  fprintf( debug_file, "Compute directly (%d %d %d %d %d)\n",
	   r->l, r->r, r->t, r->b, r->length );
  fflush( debug_file );
#endif
  CalcField( flags->fractal, iterData, r->l, r->r, r->t, r->b );
    /* compute the field */

  *npoints = (r->r - r->l + 1) * (r->b - r->t + 1);
  x = r->l;  y = r->t;
  for (i=0; i<*npoints; i++) {
    pointData[i].x = x++;
    pointData[i].y = y;
    pointData[i].c = Iter2Color( flags, iterData[i] );
#if DEBUG>2
    fprintf( debug_file, "computed (%d %d) %d\n", pointData[i].x,
	     pointData[i].y, pointData[i].c );
#endif
    if (x > r->r) {
      x = r->l;
      y++;
    }
  }
  return 0;
}



DrawChunk( graph, colorData, r, flags )
MPE_XGraph graph;
int *colorData;
rect r;
Flags *flags;
{
  int a, b;

  for (b=r.t; b<=r.b; b++) {
    for (a=r.l; a<=r.r; a++) {
      MPE_Draw_point( graph, a, b, *colorData );
#if DEBUG>1
      fprintf( debug_file, "put color %d at (%d %d)\n", *colorData,
	       a, b );
      fflush( debug_file );
#endif
      colorData++;
    }
  }

  if (flags->with_tracking_win) {
    MPE_Update(tracking_win);
  }

  MPE_Update(graph);
  return 0;
}


#if DEBUG>2
#define LOOP( start, toContinue, incrBefore, fn, check, lbl, incrAfter ) \
  start; \
  while (toContinue) { \
    incrBefore; \
    pointPtr->x = x; \
    pointPtr->y = y; \
    pointPtr->c = Iter2Color( flags, fn( re, im ) ); \
    fprintf( debug_file, "computed (%d %d) %d\n", pointPtr->x, \
             pointPtr->y, pointPtr->c ); \
    check \
  lbl \
    incrAfter; \
  }
#else
#define LOOP( start, toContinue, incrBefore, fn, check, lbl, incrAfter ) \
  start; \
  while (toContinue) { \
    incrBefore; \
    pointPtr->x = x; \
    pointPtr->y = y; \
    pointPtr->c = Iter2Color( flags, fn( re, im ) ); \
    check \
  lbl \
    incrAfter; \
  }
#endif

/*
    fprintf(stderr, "computed (%d %d) to be %d\n", x, y, pointPtr->c ); \
*/

/* really, all these stupid loop macros will make it easier! */

#define LOOP_TOP( fn, check, lbl ) \
  LOOP( (y=r.t, x=r.l+1), x<=r.r, NUM_ASSIGN( re, NUM_ADD( re, rstep ) ), \
        fn, check, lbl, (pointPtr++,x++) );

#define LOOP_RIGHT( fn, check, lbl ) \
  LOOP( (x=r.r, y=r.t+1), y<=r.b, NUM_ASSIGN( im, NUM_ADD( im, istep ) ), \
        fn, check, lbl, (pointPtr++,y++) );

#define LOOP_BOTTOM( fn, check, lbl ) \
  LOOP( (y=r.b, x=r.r-1), x>=r.l, NUM_ASSIGN( re, NUM_SUB( re, rstep ) ), \
        fn, check, lbl, (pointPtr++,x--) );

#define LOOP_LEFT( fn, check, lbl ) \
  LOOP( (x=r.l, y=r.b-1), y>r.t,  NUM_ASSIGN( im, NUM_SUB( im, istep ) ), \
        fn, check, lbl, (pointPtr++,y--) );

#define LOOP_TOP_CHECK( fn, lbl ) \
  LOOP_TOP( fn, if (pointPtr->c != firstColor) goto lbl;, ; );

#define LOOP_RIGHT_CHECK( fn, lbl  ) \
  LOOP_RIGHT( fn, if (pointPtr->c != firstColor) goto lbl;, ; );

#define LOOP_BOTTOM_CHECK( fn, lbl  ) \
  LOOP_BOTTOM( fn, if (pointPtr->c != firstColor) goto lbl;, ; );

#define LOOP_LEFT_CHECK( fn, lbl  ) \
  LOOP_LEFT( fn, if (pointPtr->c != firstColor) goto lbl;, ; );

#define LOOP_TOP_NOCHECK( fn, lbl  ) \
  LOOP_TOP( fn, ; , lbl: );

#define LOOP_RIGHT_NOCHECK( fn, lbl  ) \
  LOOP_RIGHT( fn, ; , lbl: );

#define LOOP_BOTTOM_NOCHECK( fn, lbl  ) \
  LOOP_BOTTOM( fn, ; , lbl: );

#define LOOP_LEFT_NOCHECK( fn, lbl  ) \
  LOOP_LEFT( fn, ; , lbl: );

#define LOOP_FN( fn, lbl1, lbl2, lbl3, lbl4 ) \
  if (r.b-r.t>1 && r.r-r.l>1) { \
    /* if there's a chance to subdivide, */ \
    LOOP_TOP_CHECK( fn, lbl1 ); \
    LOOP_RIGHT_CHECK( fn, lbl2 ); \
    LOOP_BOTTOM_CHECK( fn, lbl3 ); \
    LOOP_LEFT_CHECK( fn, lbl4 ); \
    *isContinuous = 1; \
    return 1;   /* if we made it to this point, it's continuous */ \
    LOOP_TOP_NOCHECK( fn, lbl1 ); \
    LOOP_RIGHT_NOCHECK( fn, lbl2 ); \
    LOOP_BOTTOM_NOCHECK( fn, lbl3 ); \
    LOOP_LEFT_NOCHECK( fn, lbl4 ); \
    *isContinuous = 0; \
    return 0;   /* it ain't continuous */ \
  } else { /* if there's no chance to subdivide, don't insert the checks */ \
    LOOP_TOP( fn, ; , ; ); \
    LOOP_RIGHT( fn, ; , ; ); \
    if (r.r-r.l && r.b-r.t) { \
      /* only do the opposite sides if >1 row and >1 column */ \
      LOOP_BOTTOM( fn, ; , ; ); \
      LOOP_LEFT( fn, ; , ; ); \
    } \
    *isContinuous = 0; \
    return 0;  /* it may or may not be continuous, doesn't matter */ \
  }



int ComputeBorder( winspecs, flags, rectPtr, pointData, maxnpoints,
		   npoints, isContinuous )
Winspecs *winspecs;
Flags *flags;
rect *rectPtr;
MPE_Point *pointData;
int maxnpoints, *npoints, *isContinuous;
{
  register NUM re, im, rstep, istep;
  register int x, y;
  register MPE_Point *pointPtr;
  register MPE_Color firstColor;
  rect r;

  r = *rectPtr;
  /* xsplit, ysplit - where to split the rectangle */

    /* set the complex points */
  NUM_ASSIGN( re, COORD2CMPLX( flags->rmin, flags->rmax, 0,
			       winspecs->width-1,  r.l) );
  NUM_ASSIGN( im, COORD2CMPLX( flags->imax, flags->imin, 0,
			       winspecs->height-1, r.t) );
  NUM_ASSIGN( rstep,  NUM_DIV( NUM_SUB( flags->rmax, flags->rmin ),
			       INT2NUM( winspecs->width-1 ) ) );
  NUM_ASSIGN( istep,  NUM_DIV( NUM_SUB( flags->imin, flags->imax ),
			       INT2NUM( winspecs->height-1 ) ) );

  pointPtr = pointData+1;
  pointData->x = r.l;
  pointData->y = r.t;
  pointData->c = firstColor = Iter2Color( flags,
		 (flags->fractal == MBROT) ? MbrotCalcIter( re, im ) :
		 (flags->fractal == JULIA) ? JuliaCalcIter( re, im ) :
		                             MbrotCalcIter( re, im ) );
#if DEBUG>2
  fprintf( debug_file, "computed (%d %d) %d\n",
	   pointData->x, pointData->y, pointData->c );
#endif

  *npoints = r.length;
    /* calculate first point */

  
  switch( flags->fractal ) {
  case MBROT:
    LOOP_FN( MbrotCalcIter, m1, m2, m3, m4 );
  case JULIA:
    LOOP_FN( JuliaCalcIter, j1, j2, j3, j4 );
  case NEWTON:
    LOOP_FN( MbrotCalcIter, n1, n2, n3, n4 );
  }
  return 0;
}

void
DrawBorder( graph, colorData, r )
MPE_XGraph graph;
int *colorData;
rect r;
{
  int x, y;

  for (y=r.t, x=r.l; x<=r.r; x++) {
#if DEBUG_POINTS
    fprintf( debug_file, "draw %d at %d %d\n", *colorData, x, y );
    fflush( debug_file );
#endif
    MPE_Draw_point( graph, x, y, *colorData );
    colorData++;
  }
  for (x=r.r, y=r.t+1; y<=r.b; y++) {
#if DEBUG_POINTS
    fprintf( debug_file, "draw %d at %d %d\n", *colorData, x, y );
    fflush( debug_file );
#endif
    MPE_Draw_point( graph, x, y, *colorData );
    colorData++;
  }
  if (r.r-r.l && r.b-r.t) {
    for (y=r.b, x=r.r-1;x>=r.l; x--) {
#if DEBUG_POINTS
      fprintf( debug_file, "draw %d at %d %d\n", *colorData, x, y );
      fflush( debug_file );
#endif
      MPE_Draw_point( graph, x, y, *colorData );
      colorData++;
    }
    for (x=r.l, y=r.b-1; y>r.t; y--) {
#if DEBUG_POINTS
      fprintf( debug_file, "draw %d at %d %d\n", *colorData, x, y );
      fflush( debug_file );
#endif
      MPE_Draw_point( graph, x, y, *colorData );
      colorData++;
    }
  }
  MPE_Update( graph );
}

void
DrawBlock( graph, pointData, r )
MPE_XGraph graph;
MPE_Point *pointData;
rect *r;
{
#if DEBUG>2
  int x, y, i;

  i = 0;
  for (y=r->t; y<=r->b; y++) {
    for (x=r->l; x<=r->r; x++) {
      fprintf( debug_file, "drawing (%d %d) %d\n", x, y, pointData->c );
      i++;
    }
  }
  fflush( debug_file );
#endif

  MPE_Fill_rectangle( graph, r->l, r->t, r->r - r->l + 1, r->b - r->t + 1,
		      pointData->c );
  
  MPE_Update( graph );
}


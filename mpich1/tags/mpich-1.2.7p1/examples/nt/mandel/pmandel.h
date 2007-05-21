

#ifndef _PM_H_
#define _PM_H_

#include "mpe_graphics.h"
#include "mpi.h"
//#include "mpe.h"
#include "fract_gen.h"

extern bool g_bNoStretch;

#define LOG 0
#define MAX_RECT_PASSED 4

extern MPI_Datatype winspecs_type, flags_type, NUM_type, rect_type;
extern FILE *debug_file;

typedef enum _Algorithms {
  alg_block,
  alg_separate_rect,
  alg_solid_rect
} Alogrithms;

typedef struct _Winspecs {
  int height, width;		/* size of the window */
  int bw;			/* whether to draw in black&white */
  int xpos, ypos;		/* position of the window */
  int numColors;		/* number of colors to use */
  MPE_Color *colorArray;	/* colors */
} Winspecs;

typedef struct _Flags {
  char *logfile;		/* name of logfile (NULL for no logging) */
  char *inf;			/* input file (NULL for no input file) */
  char *outf;			/* output file (NULL for no output file) */
  Winspecs *winspecs;		/* so we only need to pass one */
  int breakout;			/* when to stop subdividing */
  int randomize;		/* whether to proceed in a random order */
  int colReduceFactor;		/* how many iteration levels each color will
				   span */
  int loop;			/* continually loop through input file */
  int zoom;			/* ask for zoom rectangle after each */
  int askNeighbor;		/* in alg_solid_rect, whether to ask neighbor
				   or master for work */
  int sendMasterComplexity;	/* in alg_solid_rect with !askNeighbor,
				   whether to send the master the complexity
				   of the region you need computed */
  int drawBlockRegion;		/* in alg_solid_rect, whether to draw the
				   region computed at once or to
				   wait and draw a complete rectangle */
  //int fractal;			/* fractal type-MBROT, JULIA, or NEWTON */
  Fractal_type fractal;
  int maxiter;			/* bailout point  */
  double boundary_sq;		/* boundary for JULIA & MBROT */
  double epsilon;		/* epsilon for NEWTON */
  NUM rmin, rmax, imin, imax;	/* region to be computed */
  NUM julia_r, julia_i;		/* point the Julia set is related to */
} Flags;


/* logfile events */
#define S_COMPUTE 10
#define E_COMPUTE 11
#define S_DRAW_BLOCK 12
#define E_DRAW_BLOCK 13
#define S_WAIT_FOR_MESSAGE 14
#define E_WAIT_FOR_MESSAGE 15
#define S_DRAW_RECT 16
#define E_DRAW_RECT 17
#define S_DRAW_CHUNK 18
#define E_DRAW_CHUNK 19
#define SEND_RECTS 20


/* defaults: */
#define DEF_height    768
#define DEF_width     768
#define DEF_bw        0
#define DEF_xpos      -1
#define DEF_ypos      -1
//#define DEF_numColors 16
#define DEF_numColors 64

#define DEF_logfile   0
#define DEF_inf       0
#define DEF_outf      0
#define DEF_breakout  12
#define DEF_randomize 1
//#define DEF_colReduceFactor 4
#define DEF_colReduceFactor 1
#define DEF_loop      0
#define DEF_zoom      1
#define DEF_askNeighbor 1
#define DEF_sendMasterComplexity 0
#define DEF_drawBlockRegion 1
#define DEF_fractal   MBROT
#define DEF_maxiter   1000
#define DEF_boundary  2.0
#define DEF_epsilon   .01
/*
#define DEF_rmin      -.8
#define DEF_rmax      -.7
#define DEF_imin      .05
#define DEF_imax      .15
*/
#define DEF_rmin      -2.0
#define DEF_rmax      2.0
#define DEF_imin      -2.0
#define DEF_imax      2.0
#define DEF_julia_r   .331
#define DEF_julia_i   -.4

typedef struct {
  int l, r, t, b, length;
  /* length =  (r.r-r.l+1) * (r.b-r.t+1) */
} rect;

typedef struct {
  int head, tail, size, randomPt, randomize;
  rect *r;
} rect_queue;

#define MASTER_PROC 0

/* messge tags: */
/*    master to slave: */
#define WINDOW_CLOSED     39
#define SENDING_RECTANGLE 40
#define SENDING_POINTS    41
#define READY_TO_START    42
#define READY_FOR_MORE    43
#define ADD2Q             44
#define RECTS_TO_ENQUEUE  45
/*    slave to master: */
#define ASSIGNMENT        46
#define ALL_DONE          47

#if LOG 
#define MPE_LOG_SEND( to, tag, size ) \
  if (flags->logfile) MPE_Log_send( to, tag, size )

#define MPE_LOG_RECEIVE( from, tag, size ) \
  if (flags->logfile) MPE_Log_receive( from, tag, size )

#define MPE_LOG_EVENT( event, data, str ) \
  if (flags->logfile) MPE_Log_event( event, data, str )

#define MPE_DESCRIBE_STATE( start, end, name , color ) \
  if (flags->logfile) MPE_Describe_state( start, end, name , color )

#define MPE_DESCRIBE_EVENT( event, name ) \
  if (flags->logfile) MPE_Describe_event( event, name )

#define MPE_INIT_LOG() \
  if (flags->logfile) MPE_Init_log()

#define MPE_FINISH_LOG( file ) \
  if (flags->logfile) MPE_Finish_log( file )

#else
#define MPE_LOG_SEND( to, tag, size ) {}
#define MPE_LOG_RECEIVE( from, tag, size ) {}
#define MPE_LOG_EVENT( event, data, str ) {}
#define MPE_DESCRIBE_STATE( start, end, name , color ) {}
#define MPE_DESCRIBE_EVENT( event, name ) {}
#define MPE_INIT_LOG() {}
#define MPE_FINISH_LOG( file ) {}
#endif

#endif
/* _PM_H_ */


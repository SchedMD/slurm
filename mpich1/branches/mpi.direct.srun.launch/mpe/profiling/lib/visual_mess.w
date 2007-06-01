#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif

#include <stdio.h>

#include <math.h>

#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include "mpi.h"
#include "mpe.h"
#include "point.h"

#define DEBUG_{{fileno}} 0

static int procid_{{fileno}}, np_{{fileno}}, readyToDraw_{{fileno}}=0;
static int inProfile_{{fileno}}=0, xpos_{{fileno}}=-1, ypos_{{fileno}}=-1;
static point *procCoords_{{fileno}};
static MPE_XGraph prof_graph_{{fileno}};
static MPE_Color drawColor_{{fileno}};

#define PROC_RADIUS_{{fileno}}     10
#define PROC_SEPARATION_{{fileno}} 40
#define ARROW_LENGTH_{{fileno}}    12
#define ARROW_WIDTH_{{fileno}}      5
#define MARGIN_{{fileno}}           1.2

static vector SubPoints_{{fileno}}(a, b)
point a, b;
{
  vector c;
  c.x = a.x - b.x;
  c.y = a.y - b.y;
  return c;
}


#define UnitFromEndpoints_{{fileno}}( unit, start, end ) { \
  register double x, y, mag; \
  x = (end).x-(start).x; \
  y = (end).y-(start).y; \
  mag = sqrt( x*x + y*y ); \
  (unit).x = x/mag; \
  (unit).y = y/mag; }

#define NormVector_{{fileno}}( norm, vector ) { \
  (norm).x = -(vector).y; \
  (norm).y = vector.x; }

#define AddPointMultVector_{{fileno}}( newPt, pt, vec2, factor ) { \
  (newPt).x = (pt).x + (vec2).x*(factor); \
  (newPt).y = (pt).y + (vec2).y*(factor); }



void DrawScreen_{{fileno}}( procid, np ) {
  int width, procNum, radius;
  double x, y, angle;

  readyToDraw_{{fileno}} = 0;

  procCoords_{{fileno}} = (point *) malloc( sizeof( point ) * np );
  radius = (PROC_SEPARATION_{{fileno}}*np)/3.1416;
  width =  (radius + PROC_RADIUS_{{fileno}}) * 2 *
                      MARGIN_{{fileno}};

  MPE_Open_graphics( &prof_graph_{{fileno}}, MPI_COMM_WORLD, 0,
		     xpos_{{fileno}}, ypos_{{fileno}}, width,
		     width, 0 );

  readyToDraw_{{fileno}} = 1;

  if (procid == 0)
    MPE_Fill_rectangle( prof_graph_{{fileno}}, 0, 0, width,
		        width, MPE_WHITE );


  MPE_Draw_logic( prof_graph_{{fileno}}, MPE_LOGIC_INVERT );
  for (procNum=0; procNum < np; procNum++) {
    angle = (((double)procNum)/np)*3.1416*2 + 3.1416/2;
    procCoords_{{fileno}}[procNum].x = width/2 + radius
      * cos( angle );
    procCoords_{{fileno}}[procNum].y = width/2 - radius
      * sin( angle );
    if (procid_{{fileno}} == 0) {
      MPE_Draw_circle( prof_graph_{{fileno}},
		       procCoords_{{fileno}}[procNum].x,
		       procCoords_{{fileno}}[procNum].y,
		       PROC_RADIUS_{{fileno}}, MPE_BLACK );
    }
  }
  MPE_Update( prof_graph_{{fileno}} );
}



{{fn fn_name MPI_Init}}
  {{callfn}}
  MPI_Comm_rank( MPI_COMM_WORLD, &procid_{{fileno}} );
  MPI_Comm_size( MPI_COMM_WORLD, &np_{{fileno}} );
  MPI_Barrier( MPI_COMM_WORLD );

  DrawScreen_{{fileno}}( procid_{{fileno}}, np_{{fileno}} );
{{endfn}}


int prof_send( sender, receiver, tag, size, note )
int sender, receiver, tag, size;
char *note;
{
  MPE_Prof_DrawArrow_{{fileno}}( sender, receiver );
  return 0;
}

int prof_recv( receiver, sender, tag, size, note )
int receiver, sender, tag, size;
char *note;
{
  MPE_Prof_DrawArrow_{{fileno}}( sender, receiver );
  return 0;
}



{{fn fn_name MPI_finalize}}
  MPE_Close_graphics( &prof_graph_{{fileno}} );
  {{callfn}}
{{endfn}}


static MPE_Prof_DrawArrow_{{fileno}}( fromProc, toProc )
int fromProc, toProc;
{
  point start, end, a, b, c, d, e;
  vector unit, norm;

/*
                     D
                     | \
  A------------------B  E
                     | /
                     C

*/

  if (!readyToDraw_{{fileno}}) return;
  start = procCoords_{{fileno}}[fromProc];
  end = procCoords_{{fileno}}[toProc];
  UnitFromEndpoints_{{fileno}}( unit, start, end );
  NormVector_{{fileno}}( norm, unit );

  AddPointMultVector_{{fileno}}( a, start, unit,  PROC_RADIUS_{{fileno}} );
  AddPointMultVector_{{fileno}}( e, end,   unit, -PROC_RADIUS_{{fileno}} );
  AddPointMultVector_{{fileno}}( b, e,     unit, -ARROW_LENGTH_{{fileno}} );
  AddPointMultVector_{{fileno}}( c, b,     norm,  ARROW_WIDTH_{{fileno}} );
  AddPointMultVector_{{fileno}}( d, b,     norm, -ARROW_WIDTH_{{fileno}} );

  MPE_Draw_line( prof_graph_{{fileno}}, a.x, a.y, b.x, b.y, MPE_BLACK );
  MPE_Draw_line( prof_graph_{{fileno}}, c.x, c.y, d.x, d.y, MPE_BLACK );
  MPE_Draw_line( prof_graph_{{fileno}}, d.x, d.y, e.x, e.y, MPE_BLACK );
  MPE_Draw_line( prof_graph_{{fileno}}, e.x, e.y, c.x, c.y, MPE_BLACK );
  MPE_Update( prof_graph_{{fileno}} );
}



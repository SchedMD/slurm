#ifndef _MPE_GRAPHICS_
#define _MPE_GRAPHICS_

#ifdef MPE_NOMPI
typedef int MPI_Comm;
#else
#include "mpi.h"
#endif

#include <windows.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

#define ANSI_ARGS(a) a

/* See colornames in baseclr.h */
/*
typedef enum { MPE_WHITE = 0, MPE_BLACK = 1, MPE_RED = 2, MPE_YELLOW = 3, 
	       MPE_GREEN = 4, MPE_CYAN = 5, MPE_BLUE = 6, MPE_MAGENTA = 7,
               MPE_AQUAMARINE = 8, MPE_FORESTGREEN = 9, MPE_ORANGE = 10, 
	       MPE_MAROON = 11, MPE_BROWN = 12, MPE_PINK = 13, MPE_CORAL = 14, 
               MPE_GRAY = 15} MPE_Color;
//*/
#define MPE_Color			COLORREF
#define MPE_WHITE			RGB(255,255,255)
#define MPE_BLACK			RGB(0,0,0)
#define MPE_RED				RGB(255,0,0)
#define MPE_YELLOW			RGB(255,255,0)
#define MPE_GREEN			RGB(0,255,0)
#define MPE_CYAN			RGB(0,255,255)
#define MPE_BLUE			RGB(0,0,255)
#define MPE_MAGENTA			RGB(255,0,255)
#define MPE_AQUAMARINE		RGB(255,128,255)
#define MPE_FORESTGREEN		RGB(0,128,0)
#define MPE_ORANGE			RGB(200,128,0)
#define MPE_MAROON			RGB(200,0,255)
#define MPE_BROWN			RGB(128,128,64)
#define MPE_PINK			RGB(255,128,128)
#define MPE_CORAL			RGB(255,255,128)
#define MPE_GRAY			RGB(128,128,128)

extern int MPE_buttonArray[];
extern int MPE_logicArray[];

  /* given existing pixel 'dst' and new, overlapping pixel 'src' */
#define MPE_LOGIC_CLEAR        (MPE_logicArray[0])
#define MPE_LOGIC_AND          (MPE_logicArray[1])
#define MPE_LOGIC_ANDREVERSE   (MPE_logicArray[2])
#define MPE_LOGIC_COPY         (MPE_logicArray[3])
#define MPE_LOGIC_ANDINVERTED  (MPE_logicArray[4])
#define MPE_LOGIC_NOOP         (MPE_logicArray[5])
#define MPE_LOGIC_XOR          (MPE_logicArray[6])
#define MPE_LOGIC_OR           (MPE_logicArray[7])
#define MPE_LOGIC_NOR          (MPE_logicArray[8])
#define MPE_LOGIC_EQUIV        (MPE_logicArray[9])
#define MPE_LOGIC_INVERT       (MPE_logicArray[10])
#define MPE_LOGIC_ORREVERSE    (MPE_logicArray[11])
#define MPE_LOGIC_COPYINVERTED (MPE_logicArray[12])
#define MPE_LOGIC_ORINVERTED   (MPE_logicArray[13])
#define MPE_LOGIC_NAND         (MPE_logicArray[14])
#define MPE_LOGIC_SET          (MPE_logicArray[15])

#define MPE_BUTTON1 (MPE_buttonArray[0])
#define MPE_BUTTON2 (MPE_buttonArray[1])
#define MPE_BUTTON3 (MPE_buttonArray[2])
#define MPE_BUTTON4 (MPE_buttonArray[3])
#define MPE_BUTTON5 (MPE_buttonArray[4])


/* types of visuals for Get_drag_region */
#define MPE_DRAG_NONE 0		     /* no visual */
#define MPE_DRAG_RECT 1		     /* rubber band box */
#define MPE_DRAG_LINE 2		     /* rubber band line */
#define MPE_DRAG_CIRCLE_RADIUS 3     /* rubber band circle, */
    /* one point is the center of the circle, other point is on the circle */
#define MPE_DRAG_CIRCLE_DIAMETER 4
    /* each point is on opposite sides of the circle */
#define MPE_DRAG_CIRCLE_BBOX 5
    /* the two points define a bounding box inside which is drawn a circle */
#define MPE_DRAG_OVAL_BBOX 6
    /* the two points define a bounding box inside which is drawn an oval */
#define MPE_DRAG_SQUARE 7


#ifdef MPE_INTERNAL
#include "mpetools.h"
#include "basex11.h"

typedef struct MPE_XGraph_s *MPE_XGraph;
struct MPE_XGraph_s {
  int      Cookie;
  XBWindow *xwin;
  int      backingStore;	/* NotUseful, WhenMapped, or Always */
  MPI_Comm comm;
  int      is_collective;
  char     *display_name;       /* Used to allow us to run other tools ... */
  char     *capture_file;       /* Used to capture output at update */
  int      capture_num, 
           capture_cnt, 
           capture_freq;
  /* 
     The following are for event-driven input.
     This simple interface allows an advanced user to always watch for
     certain events (like keypress) without requiring all code 
     We also need to define a wait-for-user-event routine 

     The alternative is to have a chain of event handlers; adding one
     creates a new entry in the chain.  For this, there needs to be
     an event_routine structure, that has (user routine) and (next).
   */
  long     input_mask;          /* Input mask of enabled events */
    /* Routine to call for events */
  int      (*event_routine) ANSI_ARGS(( MPE_XGraph, XEvent * ));  
};
#define MPE_G_COOKIE 0xfeeddada

#define MPE_XEVT_IDLE_MASK 0
/* normal XEvent mask; what it should be set to during normal processing */
/* Eventually, this should be ExposureMask or more */

#else

//typedef void *MPE_XGraph;
struct MPE_XGraph
{
	int width, height;
	MPE_Color *map;
	bool bVisible;
	HWND hWnd;
	HDC hDC;
	HGDIOBJ hOldBitmap;
};

#endif

typedef struct MPE_Point_ {
  int x, y;
  MPE_Color c;
} MPE_Point;

#define MPE_GRAPH_INDEPDENT  0
#define MPE_GRAPH_COLLECTIVE 1

//extern int MPE_Open_graphics ANSI_ARGS(( MPE_XGraph *handle, MPI_Comm comm, char *display, int x, int y, int w, int h, int is_collective ));
int MPE_Open_graphics(MPE_XGraph *graph, MPI_Comm comm, char *display, int x, int y, int width, int height, int iscollective);

//extern int MPE_Draw_point ANSI_ARGS(( MPE_XGraph handle, int x, int y, MPE_Color color ));
int MPE_Draw_point(MPE_XGraph &graph, int x, int y, MPE_Color color);

extern int MPE_Draw_line ANSI_ARGS(( MPE_XGraph handle, int x1, int y1,
	   int x2, int y2, MPE_Color color ));

extern int MPE_Draw_circle ANSI_ARGS(( MPE_XGraph, int, int, int, MPE_Color ));

extern int MPE_Draw_string ANSI_ARGS(( MPE_XGraph, int, int, MPE_Color,
				       char * ));

//extern int MPE_Fill_rectangle ANSI_ARGS(( MPE_XGraph handle, int x, int y, int w, int h, MPE_Color color ));
int MPE_Fill_rectangle(MPE_XGraph &graph, int x, int y, int width, int height, MPE_Color color);

//extern int MPE_Update ANSI_ARGS(( MPE_XGraph handle ));
int MPE_Update(MPE_XGraph &graph);

extern int MPE_Num_colors ANSI_ARGS(( MPE_XGraph handle, int *nc ));

//extern int MPE_Make_color_array ANSI_ARGS(( MPE_XGraph handle, int ncolors, MPE_Color array[] ));
int MPE_Make_color_array(MPE_XGraph &graph, int num_colors, MPE_Color colors[]);

//extern int MPE_Close_graphics ANSI_ARGS(( MPE_XGraph *handle ));
int MPE_Close_graphics(MPE_XGraph *graph);

extern int MPE_CaptureFile ANSI_ARGS(( MPE_XGraph, char *, int ));

//extern int MPE_Draw_points ANSI_ARGS(( MPE_XGraph, MPE_Point *, int ));
int MPE_Draw_points(MPE_XGraph &graph, MPE_Point *points, int num_points);

extern int MPE_Fill_circle ANSI_ARGS(( MPE_XGraph, int, int, int, MPE_Color ));

extern int MPE_Draw_logic  ANSI_ARGS(( MPE_XGraph, int ));

extern int MPE_Line_thickness ANSI_ARGS(( MPE_XGraph, int ));

extern int MPE_Draw_dashes ANSI_ARGS(( MPE_XGraph, int ));

extern int MPE_Dash_offset ANSI_ARGS(( MPE_XGraph, int ));

extern int MPE_Add_RGB_color ANSI_ARGS(( MPE_XGraph, int, int, int, 
					 MPE_Color * ));

extern int MPE_Xerror ANSI_ARGS(( int, char * ));

/* xmouse */
extern int MPE_Get_mouse_press ANSI_ARGS(( MPE_XGraph, int *, int *, int * ));
extern int MPE_Iget_mouse_press ANSI_ARGS(( MPE_XGraph, int *, int *, int *,
					    int * ));
extern int MPE_Get_drag_region ANSI_ARGS(( MPE_XGraph, int, int, 
					   int *, int *, int *, int * ));
extern int MPE_Get_drag_region_fixratio ANSI_ARGS(( MPE_XGraph, int, double, 
						    int *, int *, 
						    int *, int * ));

#endif


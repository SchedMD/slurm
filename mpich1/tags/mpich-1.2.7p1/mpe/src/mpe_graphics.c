#include <math.h>
#include "mpeconf.h"
#include "mpetools.h" 
#include "basex11.h"  

#ifdef MPE_NOMPI
#define MPI_MAX_PROCESSOR_NAME 256
#else
#include "mpi.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#else
extern char *getenv();
#endif

#if defined(HAVE_STRING_H) || defined(STDC_HEADERS)
#include <string.h>
#endif

#define MPE_INTERNAL
#include "mpe.h"        /*I "mpe.h" I*/

#define DEBUG 0

typedef struct xpand_list_Int_ {
  int *list;
  int nused;
  int size;
} xpand_list_Int;

#define ListHeadPtr( listPtr ) ( (listPtr)->list )

#define ListSize( listPtr ) ( (listPtr)->nused )

#define ListClose( listPtr, headPtr, nitems ) { \
  headPtr = ListHeadPtr( listPtr ); \
  nitems = ListSize( listPtr ); \
  free( listPtr ); \
}

static xpand_list_Int *Int_CreateList(int initialLen);

/* Forward refs */
static void SortPoints ( MPE_Point *, int, XPoint **, MPE_Color **,
				   int **, int * );
static int Int_AddItem ( xpand_list_Int *, int );

int MPE_buttonArray[] = {
  Button1,
  Button2,
  Button3,
  Button4,
  Button5
};

int MPE_logicArray[] = {
  GXclear,			/* 0 */
  GXand,			/* src && dst */
  GXandReverse,			/* src && !dst */
  GXcopy,			/* src */
  GXandInverted,		/* !src && dst */
  GXnoop,			/* dst */
  GXxor,			/* src XOR dst */
  GXor,				/* src || !dst */
  GXnor,			/* !src && !dst */
  GXequiv,			/* !src XOR dst */
  GXinvert,			/* !dst */
  GXorReverse,			/* !src || dst */
  GXcopyInverted,		/* !src */
  GXorInverted,			/* !src || dst */
  GXnand,			/* !src || !dst */
  GXset				/* 1 */
}; 

static void SetBackingStoreBitGrav (MPE_XGraph);

#ifdef POINTER_64_BITS  
static int fort_index = 0;
MPE_XGraph MPE_fort_head = 0;
#endif

/*N XGRAPHICS_FORTRAN

    Notes For Fortran Interface :
    The Fortran interface to this routine is different from its C
    counterpart and it has an additional argument, ierr, at the end
    of the argument list, i.e. the returned function value (the error
    code) in C interface is returned as the additional argument in
    Fortran interface.  The Fortran interface is invoked with the 
    CALL statement.

    All MPI and MPE objects, MPI_Comm, MPE_XGraph and MPE_Color, are 
    of type INTEGER in Fortran.
N*/

/*@
    MPE_Open_graphics - (collectively) opens an X Windows display

    Input Parameters:
+   comm - Communicator of participating processes
.   display - Name of X window display.  If null, display will be taken from
    the DISPLAY variable on the process with rank 0 in 'comm'.  If that is
    either undefined, or starts with w ":", then the value of display is
    ``hostname``:0
.   x,y - position of the window.  If '(-1,-1)', then the user should be
    asked to position the window (this is a window manager issue).
.   w,h - width and height of the window, in pixels.
-   is_collective - true if the graphics operations are collective; this
    allows the MPE graphics operations to make fewer connections to the 
    display.  If false, then all processes in the communicator comm will 
    open the display; this could exceed the number of connections that your
    X window server allows.  Not yet implemented.

    Output Parameter:
.   handle - Graphics handle to be given to other MPE graphics routines.

    Notes:
    This is a collective routine.  All processes in the given communicator
    must call it, and it has the same semantics as 'MPI_Barrier' (that is,
    other collective operations can not cross this routine).

.N XGRAPHICS_FORTRAN

    Additional Notes for Fortran Interface :
    If Fortran 'display' argument is an empty string, "", display will be
    taken from the DISPLAY variable on the process with rank 0 in 'comm'.
    The trailing blanks in Fortran CHARACTER string argument will be
    ignored.
@*/
int MPE_Open_graphics( handle, comm, display, x, y, w, h, is_collective )
MPE_XGraph *handle;
MPI_Comm   comm;
char       display[MPI_MAX_PROCESSOR_NAME+4];
int        x, y;
int        w, h;
int        is_collective;
{

#ifndef MPE_NOMPI
  Window     win;
#endif
  MPE_XGraph new;
#ifdef FOO
  XFontStruct **font_info;
  XGCValues  values;
  char       fontname[128];
#endif
#ifndef MPE_NOMPI
  int        numprocs, namelen;
#endif
  int        myid, successful=0;
  
  myid = 0;		     /* for the single processor version */
  *handle            = 0;    /* In case of errors */
  new		     = NEW(struct MPE_XGraph_s);    /* CHKPTRN(new); */
  new->Cookie        = MPE_G_COOKIE;
  new->xwin          = NEW(XBWindow);      /* CHKPTRN(new->xwin); */

  /* These are used to capture the images into xwd files */
  new->capture_file  = 0;
  new->capture_freq  = 1;
  new->capture_num   = 0;
  new->capture_cnt   = 0;
  new->input_mask    = 0;
  new->event_routine = 0;

#ifdef POINTER_64_BITS
  /* In case we need to support fortran */
  new->fort_index = fort_index++;
  new->next       = MPE_fort_head;
  MPE_fort_head   = new;
#endif
  
#ifndef MPE_NOMPI
  if (is_collective) {
    /* Not supported; just use individual connections */
    is_collective = 0;
  }
  
  new->comm	     = comm;
  new->is_collective = is_collective;

  MPI_Comm_size(comm,&numprocs);
  MPI_Comm_rank(comm,&myid);
#endif

  if (!display) {
#ifndef MPE_NOMPI
    int str_len;
#endif

#if DEBUG
    fprintf( stderr, "[%d] Guessing at display name.\n", myid );
    fflush( stderr );
#endif

    if (myid == 0) {
      display = getenv( "DISPLAY" );

#if DEBUG
      fprintf( stderr, "$DISPLAY = %s\n", display );
      fflush( stderr );
#endif

      if (!display || display[0] == ':') {
	/* Replace display with hostname:0 */

#ifdef MPE_NOMPI
	display = (char *)malloc( 100 );
	MPE_GetHostName( display, 100 );
#else
	/* This is not correct, since there is no guarentee that this
	   is the "correct" network name */
	display = (char *)malloc( MPI_MAX_PROCESSOR_NAME );
	MPI_Get_processor_name( display, &namelen );
#endif

#if DEBUG
	fprintf( stderr, "Process 0 is: %s\n", display );
	fflush( stderr );
#endif
	strcat( display, ":0" );

#if DEBUG
	fprintf( stderr, "Process 0 is: %s\n", display );
	fflush( stderr );
#endif

      }

#ifndef MPE_NOMPI
      str_len = strlen( display ) + 1;
      MPI_Bcast( &str_len, 1, MPI_INT, 0, comm );
#endif

    } 

#ifndef MPE_NOMPI
    else {
      MPI_Bcast( &str_len, 1, MPI_INT, 0, comm );
      display = (char *) malloc( sizeof( char ) * str_len );
    }
    MPI_Bcast( display, str_len, MPI_CHAR, 0, comm );
#endif

  }

  new->display_name = (char *)malloc( strlen(display) + 1 );
  if (!new->display_name)
      return MPE_ERR_LOW_MEM;
  strcpy( new->display_name, display );

#if DEBUG
  fprintf( stderr, "[%d] trying to open %s\n", myid, display );
  fflush( stderr );
#endif

  if (0 == myid) {
    successful = !XBQuickWindow( new->xwin, display, "MPE", x, y, w, h );
    /* ALWAYS send the local host */
    if (successful) {

#ifndef MPE_NOMPI
      win = new->xwin->win;
      MPI_Bcast( &win, 1, MPI_UNSIGNED_LONG, 0, comm );
#endif /* ifndef MPE_NOMPI */

    } else {

#ifndef MPE_NOMPI
      win = 0;
      MPI_Bcast( &win, 1, MPI_UNSIGNED_LONG, 0, comm );
#endif /* ifndef MPE_NOMPI */

    }
  }
#ifndef MPE_NOMPI
  else {
    MPI_Bcast( &win, 1, MPI_UNSIGNED_LONG, 0, comm );
    if (win) {  /* if process 0 connected */
      successful = !XBQuickWindowFromWindow( new->xwin, display, win );
    }
  }
#endif /* ifndef MPE_NOMPI */

#if DEBUG
  fprintf( stderr, "%s to %s from process %d.\n",
	   successful ? "Successfully connected" : "Failed to connect",
	   display, myid );
  fflush( stderr );
#endif

#ifdef FOO
  /* set a default font */
  strcpy(fontname,"fixed");
  if ((*font_info = XLoadQueryFont( new->xwin->disp, fontname )) == NULL)
  {
      fprintf( stderr, "Could not open %s font\n", fontname );
      exit(1);
  }
  values.font = (*font_info)->fid;
  XChangeGC( new->xwin->disp, new->xwin->gc.set, GCFont, &values );
  fprintf("successfully set default font\n");
#endif

  if (!successful) {
#ifndef MPE_NOMPI
    char myname[MPI_MAX_PROCESSOR_NAME];
    int mynamelen;
    MPI_Get_processor_name( myname, &mynamelen );
    fprintf( stderr, "Failed to connect to %s from %s\n", 
	     display, myname );
#endif
    *handle = (MPE_XGraph) 0;
    return MPE_ERR_NOXCONNECT;
  } else {
    SetBackingStoreBitGrav( new );
    *handle = new;
    return MPE_SUCCESS;
  }
}


static void SetBackingStoreBitGrav( MPE_XGraph graph )
{
  XSetWindowAttributes attrib;

  graph->backingStore = DoesBackingStore(
	ScreenOfDisplay( graph->xwin->disp, graph->xwin->screen ) );
#if DEBUG
  fprintf( stderr, "Screen's backing store support is %s.\n",
	  graph->backingStore==NotUseful ? "NotUseful" :
	  graph->backingStore==WhenMapped ? "WhenMapped" : 
	  graph->backingStore==Always ? "Always" : "dunno" );
  fflush( stderr );
#endif
  attrib.bit_gravity = NorthWestGravity;
  attrib.backing_store = graph->backingStore;
  XChangeWindowAttributes( graph->xwin->disp, graph->xwin->win,
			   CWBitGravity | CWBackingStore,
			   &attrib );
}

/*@
  MPE_CaptureFile - Sets the base filename used to capture output from updates

  Input Parameters:
+ handle - MPE graphics handle
. fname  - base file name (see below)
- freq   - Frequency of updates

  Return Values:
+ MPE_ERR_LOW_MEM - malloc for copy of the filename (fname) failed
. MPE_ERR_BAD_ARGS - 'handle' parameter is bad
- MPE_SUCCESS - success

  Notes:
  The output is written in xwd format to 'fname%d', where '%d' is the number
  of the file (starting from zero).

.N XGRAPHICS_FORTRAN

    Additional Notes for Fortran Interface :
    The trailing blanks in Fortran 'CHARACTER' string argument will be
    ignored.
@*/
int MPE_CaptureFile( handle, fname, freq )
MPE_XGraph handle;
char       *fname;
int        freq;
{
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }
  
  handle->capture_file = (char *)malloc( strlen( fname ) + 1 );
  if (!handle->capture_file) {
      fprintf( stderr, "Could not allocate memory for filename\n" );
      return MPE_ERR_LOW_MEM;
      }
  strcpy( handle->capture_file, fname );
  handle->capture_num  = 0;
  handle->capture_freq = freq;
  handle->capture_cnt  = 0;

  return MPE_SUCCESS;
}


/*@
    MPE_Draw_point - Draws a point on an X Windows display 

    Input Parameters:
+   handle - MPE graphics handle 
.   x,y - pixel position to draw.  Coordinates are upper-left origin (standard
    X11)
-   color - Color `index` value.  See 'MPE_MakeColorArray'.  
    By default, the colors
    'MPE_WHITE', 'MPE_BLACK', 'MPE_RED', 'MPE_YELLOW', 'MPE_GREEN', 'MPE_CYAN',
    'MPE_BLUE',  'MPE_MAGENTA', 'MPE_AQUAMARINE', 
            'MPE_FORESTGREEN', 'MPE_ORANGE', 'MPE_VIOLET', 'MPE_BROWN', 
            'MPE_PINK', 'MPE_CORAL' and 'MPE_GRAY' are defined.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Draw_point( handle, x, y, color )
MPE_XGraph handle;
int        x, y;
MPE_Color  color;
{
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

#ifndef MPE_NOMPI
  if (handle->is_collective) {
  } else
#endif

  {
    XBSetPixVal( handle->xwin, handle->xwin->cmapping[color] );
    XDrawPoint( handle->xwin->disp, handle->xwin->win, 
	       handle->xwin->gc.set, x, y );
  }
  return MPE_SUCCESS;
}


/*@
    MPE_Draw_points - Draws points on an X Windows display 

    Input Parameters:
+   handle - MPE graphics handle 
.   points - list of points to draw
-   npoints - number of points to draw

.N XGRAPHICS_FORTRAN
@*/
int MPE_Draw_points( handle, points, npoints )
MPE_XGraph handle;
MPE_Point *points;
int npoints;
{
  XPoint *sortedPoints;
  int *colorRanges, ncolors, colorNum, n;
  MPE_Color *colorList;

  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

#if 0
  /* temporary debug */
  {int i;
  fprintf( stderr, "MPE_Draw_points\n" );
  for (i=0; i<npoints; i++) {
    fprintf( stderr, "%d. (%d %d) %d\n", i, points[i].x, points[i].y,
	     points[i].c );
  }}
#endif
    

  SortPoints( points, npoints, &sortedPoints, &colorList, &colorRanges,
	      &ncolors );

#if 0
  /* another temporary debug */
  fprintf( stderr, "Sorted points.  %d colors\n", ncolors );
  for (i=0; i<ncolors; i++) {
    fprintf( stderr, "%d %d's.\n", colorRanges[i], colorList[i] );
  }
#endif

  for (colorNum = 0; colorNum < ncolors; colorNum++) {
    n = (colorNum < ncolors-1)
      ? colorRanges[colorNum+1] - colorRanges[colorNum]
      : npoints - colorRanges[colorNum];

/*
    printf( "xxx: %d %d %d %d\n", (colorNum < ncolors-1),
	   colorRanges[colorNum+1] - colorRanges[colorNum],
	   ncolors - colorNum,
	   (colorNum < ncolors-1)
      ? colorRanges[colorNum+1] - colorRanges[colorNum]
      : npoints - colorRanges[colorNum] );

    printf( "Sending %d points of color %d starting at %d\n", n,
	    colorList[colorNum],
	    colorRanges[colorNum] );
    

    for (i = 0; i<n; i++) {
      printf( "sending (%hd %hd)\n", 
	      sortedPoints[colorRanges[colorNum]+i].x,
	      sortedPoints[colorRanges[colorNum]+i].y );
    }
*/

    XBSetPixVal( handle->xwin, handle->xwin->cmapping[colorList[colorNum]] );
    XDrawPoints( handle->xwin->disp, handle->xwin->win, 
		 handle->xwin->gc.set,
		 sortedPoints + colorRanges[colorNum],
		 n, CoordModeOrigin );
  }
  free( sortedPoints );
  free( colorList );
  free( colorRanges );
  return MPE_SUCCESS;
}



static void SortPoints( lista, a, listb, colorList, boundaryPoints, ncolors )
MPE_Point *lista;
XPoint **listb;
MPE_Color **colorList;
int a, **boundaryPoints, *ncolors;
{
  int top, bottom, outoforder;
  MPE_Color thisColor, tempColor, *keyList;
  
  XPoint *list, tempPt;
  xpand_list_Int *boundaryList;

  boundaryList = Int_CreateList( 20 );
  list = *listb = (XPoint *) malloc( sizeof( XPoint ) * a );
  keyList = (MPE_Color *) malloc( sizeof( MPE_Color ) * a );
  for (top = 0; top < a; top++) {
    list[top].x = lista[top].x;
    list[top].y = lista[top].y;
    keyList[top] = lista[top].c;
/*
    fprintf( stderr, "unsorted %d = %d %d %d\n", top, lista[top].x,
	     lista[top].y,
	     lista[top].c );
*/
  }
  /* copy the list over */

  top = 0;
  outoforder = 1;

  while (top < a && outoforder) {
    Int_AddItem( boundaryList, top );
    /* keep a list of the start of each different color */

    /* temp. debugging */
    /* fprintf( */

    thisColor = keyList[top];
    bottom = a-1;
    outoforder = 0;

    while (top < bottom) {
      if (keyList[top] != thisColor) {
	outoforder = 1;
	while (bottom>top && keyList[bottom] != thisColor) bottom--;
	if (bottom>top) {
	  tempPt = list[bottom];
	  list[bottom] = list[top];
	  list[top] = tempPt;
	  tempColor = keyList[bottom];
	  keyList[bottom] = keyList[top];
	  keyList[top] = tempColor;
	  top++;
	}
      } else {
	top++;
      }
    }
  }

  ListClose( boundaryList, *boundaryPoints, *ncolors );

  *colorList = (MPE_Color *) malloc( sizeof( MPE_Color ) * *ncolors );
  for (top=0; top < *ncolors; top++) {
    (*colorList)[top] = keyList[(*boundaryPoints)[top]];
/*
    printf( "color %d = %d\n", top, (*colorList)[top] );
*/
  }

/*
  bottom = 0;
  for (top = 0; top < a; top++) {
    if ((*boundaryPoints)[bottom] == top) {
      fprintf( stderr, "color = %d\n", (*colorList)[bottom] );
      bottom++;
    }
    fprintf( stderr, "sorted %d = %hu %hu\n", top, list[top].x, list[top].y );
  }
*/

  free( keyList );
}



/*@
    MPE_Draw_line - Draws a line on an X11 display

    Input Parameters:
+   handle - MPE graphics handle 
.   x1,y_1 - pixel position of one end of the line to draw.  Coordinates are 
            upper-left origin (standard X11)
.   x2,y_2 - pixel position of the other end of the line to draw.  Coordinates 
            are upper-left origin (standard X11)
-   color - Color `index` value.  See 'MPE_MakeColorArray'.  
    By default, the colors
    'MPE_WHITE', 'MPE_BLACK', 'MPE_RED', 'MPE_YELLOW', 'MPE_GREEN', 'MPE_CYAN',
    'MPE_BLUE',  'MPE_MAGENTA', 'MPE_AQUAMARINE', 
            'MPE_FORESTGREEN', 'MPE_ORANGE', 'MPE_VIOLET', 'MPE_BROWN', 
            'MPE_PINK', 'MPE_CORAL' and 'MPE_GRAY' are defined.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Draw_line( handle, x1, y_1, x2, y_2, color )
MPE_XGraph handle;
int        x1, y_1, x2, y_2;
MPE_Color  color;
{
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

#ifndef MPE_NOMPI
  if (handle->is_collective) {
  } else
#endif

  {
    XBSetPixVal( handle->xwin, handle->xwin->cmapping[color] );
    XDrawLine( handle->xwin->disp, handle->xwin->win, 
	      handle->xwin->gc.set, x1, y_1, x2, y_2 );
  }
  return MPE_SUCCESS;
}

/*@
    MPE_Fill_rectangle - Draws a filled rectangle on an X11 display 

    Input Parameters:
+   handle - MPE graphics handle 
.   x,y - pixel position of the upper left (low coordinate) corner of the 
            rectangle to draw.
.   w,h - width and height of the rectangle
-   color - Color `index` value.  See 'MPE_MakeColorArray'.  
    By default, the colors
    'MPE_WHITE', 'MPE_BLACK', 'MPE_RED', 'MPE_YELLOW', 'MPE_GREEN', 'MPE_CYAN',
    'MPE_BLUE',  'MPE_MAGENTA', 'MPE_AQUAMARINE', 
            'MPE_FORESTGREEN', 'MPE_ORANGE', 'MPE_VIOLET', 'MPE_BROWN', 
            'MPE_PINK', 'MPE_CORAL' and 'MPE_GRAY' are defined.

    Notes:
    This uses the X11 definition of width and height, so you may want to 
    add 1 to both of them.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Fill_rectangle( handle, x, y, w, h, color )
MPE_XGraph handle;
int        x, y, w, h;
MPE_Color  color;
{
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

#ifndef MPE_NOMPI
  if (handle->is_collective) {
  } else
#endif

  {
    XBSetPixVal( handle->xwin, handle->xwin->cmapping[color] );
    XFillRectangle( handle->xwin->disp, handle->xwin->win, 
		   handle->xwin->gc.set, x, y, w, h );
  }
  return MPE_SUCCESS;
}



/*@
    MPE_Update - Updates an X11 display

    Input Parameter:
.   handle - MPE graphics handle.

    Note:
    Only after an 'MPE_Update' can you count on seeing the results of MPE 
    drawing routines.  This is caused by the buffering of graphics requests
    for improved performance.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Update( handle )
MPE_XGraph handle;
{
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

#ifndef MPE_NOMPI
  if (handle->is_collective) {
  } else
#endif

  {
    XFlush( handle->xwin->disp );
  }

  if (handle->capture_file) {
#ifdef HAVE_SYSTEM
      /* Place into a file */
      char cmdbuf[1024];
      if ((handle->capture_num % handle->capture_freq) == 0) {
	  /* This will need to be configured for the location of xwd ... */
	  sprintf( cmdbuf, "%sxwd -display %s -id %ld > %s%.3d.xwd\n", 
		   "/usr/local/X11R5/bin/", 
		   handle->display_name, 
		   (long) handle->xwin->win, handle->capture_file, 
		   handle->capture_cnt++ );
	  system( cmdbuf );
	  }
      handle->capture_num++;
#else
      fprintf( stderr, "Could not call system routine for file capture\n" );
#endif
      }
  return MPE_SUCCESS;
}

/*
    MPE_Create_contour - 
 */

/*
    MPE_Draw_contour - 
 */




/*@
    MPE_Close_graphics - Closes an X11 graphics device

    Input Parameter:
.   handle - MPE graphics handle.

    Return Values:
+   MPE_ERR_BAD_ARGS - 'handle' parameter is bad
-   MPE_SUCCESS - success

.N XGRAPHICS_FORTRAN
@*/
int MPE_Close_graphics( handle )
MPE_XGraph *handle;
{
  if (!handle || !*handle || (*handle)->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }
  XBWinDestroy( (*handle)->xwin );
  FREE( *handle );
  *handle = 0;
  return MPE_SUCCESS;
}




/*@
    MPE_Make_color_array - Makes an array of color indices

    Input Parameters:
+   handle - MPE graphics handle
.   nc     - Number of colors

    Output Parameter:
-   array - Array of color indices

    Notes:
    The new colors for a uniform distribution in hue space and replace the
    existing colors `except` for 'MPE_WHITE' and 'MPE_BLACK'.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Make_color_array( handle, ncolors, array )
MPE_XGraph handle;
int        ncolors;
MPE_Color  array[];
{
  int    i;
  PixVal white;
  
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }
  XBUniformHues( handle->xwin, ncolors + 2 );
  
  /* XBUniformHues creates a spectrum like:
     BLACK red->yellow->blue->purple WHITE
     We want something like:
     WHITE BLACK red->yellow->blue->purple
     
     So I shifted things around a little.
     */
  
  /* here's the original code: */
  /*
     for (i=0; i<ncolors; i++) 
     array[i] = handle->xwin->cmapping[i + 2];
     */
  
  /* here's something that works: */
  white = handle->xwin->cmapping[ncolors+1];
  for (i=0; i<ncolors; i++) {
    array[i] = (MPE_Color)(i+2);
    handle->xwin->cmapping[ncolors+1-i] = handle->xwin->cmapping[ncolors-i];
  }
  handle->xwin->cmapping[MPE_BLACK] = handle->xwin->cmapping[0];
  handle->xwin->cmapping[MPE_WHITE] = white;
  
  return MPE_SUCCESS;
}



/*@
  MPE_Num_colors - Gets the number of available colors

  Input Parameter:
. handle - MPE graphics handle

  Output Parameter:
. nc - Number of colors available on the display.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Num_colors( handle, nc )
MPE_XGraph handle;
int        *nc;
{
  if (handle->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }
  *nc = handle->xwin->maxcolors;
  return MPE_SUCCESS;
}

/*@
   MPE_Draw_circle - Draws a circle

  Input Parameters:
+ graph - MPE graphics handle
. centerx - horizontal center point of the circle
. centery - vertical center point of the circle
. radius - radius of the circle
- color - color of the circle

.N XGRAPHICS_FORTRAN
@*/
int MPE_Draw_circle( graph, centerx, centery, radius, color )
MPE_XGraph graph;
int centerx, centery, radius;
MPE_Color color;
{
  int returnVal;

  if (graph->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

#if DEBUG
  fprintf( stdout, "Drawing at (%d,%d) with radius %d and color %d\n",
	   centerx, centery, radius, color );
  fflush( stdout );
#endif
  XBSetPixVal( graph->xwin, graph->xwin->cmapping[color] );
  XDrawArc( graph->xwin->disp, XBDrawable(graph->xwin),
		        graph->xwin->gc.set, centerx-radius, centery-radius, 
		        radius*2, radius*2, 0, 360*64 );
  returnVal = 0;
  return MPE_Xerror( returnVal, "MPE_DrawCircle" );
}



/*@
   MPE_Fill_circle - Fills a circle

  Input Parameters:
+ graph - MPE graphics handle
. centerx - horizontal center point of the circle
. centery - vertical center point of the circle
. radius - radius of the circle
- color - color of the circle

.N XGRAPHICS_FORTRAN
@*/
int MPE_Fill_circle( graph, centerx, centery, radius, color )
MPE_XGraph graph;
int centerx, centery, radius;
MPE_Color color;
{
  int returnVal;

  if (graph->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

  XBSetPixVal( graph->xwin, graph->xwin->cmapping[color] );
  XFillArc( graph->xwin->disp, XBDrawable(graph->xwin),
		        graph->xwin->gc.set, centerx-radius, centery-radius, 
		        radius*2, radius*2, 0, 360*64 );
  returnVal = 0;
  return MPE_Xerror( returnVal, "MPE_FillCircle" );
}

/*@
   MPE_Draw_string - Draw a text string

  Input Parameters:
+ graph - MPE graphics handle
. x - x-coordinate of the origin of the string
. y - y-coordinate of the origin of the string
. color - color of the text
- string - text string to be drawn

.N XGRAPHICS_FORTRAN

    Additional Notes for Fortran Interface :
    The trailing blanks in Fortran CHARACTER string argument will be
    ignored.
@*/
int MPE_Draw_string( graph, x, y, color, string )
MPE_XGraph graph;
int x, y;
MPE_Color color;
char *string;
{
  int returnVal;

  if (graph->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

  printf("color = %d, string = %s\n",(int) color, string);

  XBSetPixVal( graph->xwin, graph->xwin->cmapping[color] );
  returnVal = XDrawString( graph->xwin->disp, XBDrawable(graph->xwin),
		        graph->xwin->gc.set, x, y, string, strlen(string) );
/* from mail
  returnVal = XDrawString( graph->xwin->disp, graph->xwin->win,
		        graph->xwin->gc.set, x, y, string, strlen(string) );
*/
  return MPE_Xerror( returnVal, "MPE_DrawString" );
}


/*@
   MPE_Draw_logic - Sets logical operation for laying down new pixels

  Input Parameters:
+ graph - MPE graphics handle
- function - integer specifying one of the following:
.n            'MPE_LOGIC_COPY' - no logic, just copy the pixel
.n	    'MPE_LOGIC_XOR'  - xor the new pixel with the existing one
	     and many more... see 'mpe_graphics.h'

.N XGRAPHICS_FORTRAN
@*/
int MPE_Draw_logic( graph, function )
MPE_XGraph graph;
int function;
{
  int returnVal;
  if (graph->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }
  XSetFunction( graph->xwin->disp,
			    graph->xwin->gc.set, function );
  returnVal = 0;
  MPE_Xerror( returnVal, "MPE_DrawLogic" );
  return returnVal;
}



/*@
   MPE_Line_thickness - Sets thickness of lines

  Input Parameters:
+ graph - MPE graphics handle
- thickness - integer specifying how many pixels wide lines should be

.N XGRAPHICS_FORTRAN
@*/
int MPE_Line_thickness( graph, thickness )
MPE_XGraph graph;
int thickness;
{
  XGCValues gcChanges;

  gcChanges.line_width = thickness;
  XChangeGC( graph->xwin->disp, graph->xwin->gc.set,
	     GCLineWidth, &gcChanges );

  return MPE_SUCCESS;
}



int MPE_Draw_dashes( graph, dashlen )
MPE_XGraph graph;
int dashlen;
{
  XGCValues gcChanges;

  if (dashlen) {
    gcChanges.line_style = LineDoubleDash;
    gcChanges.dashes = dashlen;
    gcChanges.dash_offset = 0;
    XChangeGC( graph->xwin->disp, graph->xwin->gc.set,
	       GCDashOffset | GCDashList | GCLineStyle, &gcChanges );
  } else {
    gcChanges.line_style = 0;
    XChangeGC( graph->xwin->disp, graph->xwin->gc.set,
	       GCLineStyle, &gcChanges );
  }

  return 0;
}


int MPE_Dash_offset( graph, offset )
MPE_XGraph graph;
int offset;
{
  XGCValues gcChanges;

  gcChanges.dash_offset = offset;
  XChangeGC( graph->xwin->disp, graph->xwin->gc.set,
	     GCDashOffset, &gcChanges );
  return 0;
}





/*@
   MPE_Add_RGB_color - Adds a color to the colormap given its RGB values

  Input Parameters:
+ graph - MPE graphics handle
- red, green, blue - color levels from 0 to 65535

  Output Parameter:
. mapping - index of the new color

  Return Values:
+ -1 - maxcolors too large (equal to numcolors)
. MPE_SUCCESS - successful
- mapping - index of the new color

  Notes:
  This call adds a color cell to X11''s color table, increments maxcolors
  (the index), and writes it to the mapping parameter.

.N XGRAPHICS_FORTRAN
@*/
int MPE_Add_RGB_color( graph, red, green, blue, mapping )
MPE_XGraph graph;
int red, green, blue;
MPE_Color *mapping;
{
  XColor colordef;

  if (graph->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

  if (graph->xwin->maxcolors == graph->xwin->numcolors)
    return -1;
  
  colordef.red   = red;
  colordef.green = green;
  colordef.blue  = blue;
  colordef.flags = DoRed | DoGreen | DoBlue;
  if (!XAllocColor( graph->xwin->disp, graph->xwin->cmap, &colordef ))
    return -1;
  graph->xwin->cmapping[graph->xwin->maxcolors] = colordef.pixel;
  *mapping = (MPE_Color) (graph->xwin->maxcolors++);
  if (graph->xwin->maxcolors == 256)
    graph->xwin->maxcolors = 255;
  return MPE_SUCCESS;
}



int MPE_Xerror( returnVal, functionName )
int returnVal;
char *functionName;
{
  if (returnVal) {
    switch (returnVal) {
    case BadAccess:
      fprintf( stderr, "'BadAccess' error in call to %s\n", functionName );
      return returnVal;
    case BadAlloc:
      fprintf( stderr, "'BadAlloc' error in call to %s\n", functionName );
      return returnVal;
    case BadColor:
      fprintf( stderr, "'BadColor' error in call to %s\n", functionName );
      return returnVal;
    case BadDrawable:
      fprintf( stderr, "'BadDrawable' error in call to %s\n", functionName );
      return returnVal;
    case BadGC:
      fprintf( stderr, "'BadGC' error in call to %s\n", functionName );
      return returnVal;
    case BadMatch:
      fprintf( stderr, "'BadMatch' error in call to %s\n", functionName );
      return returnVal;
    case BadValue:
      fprintf( stderr, "'BadValue' error in call to %s\n", functionName );
      return returnVal;
    default:
/*
      fprintf( stderr, "Unknown error %d in call to %s\n",
	       returnVal, functionName );
*/
      return returnVal;
    }
  } else {
    return MPE_SUCCESS;
  }
}


static xpand_list_Int *Int_CreateList(initialLen)
int initialLen;
{
  xpand_list_Int *tempPtr;

  if (initialLen < 1) {
    initialLen = 10;
  }
  tempPtr = (xpand_list_Int *) malloc(sizeof(xpand_list_Int));
  if (tempPtr) {
    tempPtr->list = (int *) malloc(sizeof(int) * initialLen);
    if (!tempPtr->list) {
      return 0;
    }
    tempPtr->nused = 0;
    tempPtr->size = initialLen;
  } else {
    fprintf( stderr, "Could not allocate memory for expanding list\n");
  }
  return tempPtr;
}


static int Int_AddItem(listPtr, newItem)
xpand_list_Int *listPtr;
int newItem;
{
  int *tmp;
  if (listPtr->nused == listPtr->size) {
    if (listPtr->size < 1)
      listPtr->size = 1;
    listPtr->size *= 2;
    tmp = (int *) malloc( sizeof(int) * listPtr->size);

    if (!tmp) {
      fprintf( stderr, "Ran out of memory packing pixels.\n" );
      return 1;
    } else {
      memcpy( tmp, listPtr->list, sizeof(int) * listPtr->size / 2 );
      free( listPtr->list );
      listPtr->list = tmp;
    }
  }
  listPtr->list[(listPtr->nused)++] = newItem;
  return 0;
}

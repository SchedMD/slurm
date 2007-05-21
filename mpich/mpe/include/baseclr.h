#ifndef BLACK

#define BLACK       1
#define RED         2
#define YELLOW      3
#define GREEN       4
#define CYAN        5
#define BLUE        6
#define MAGENTA     7
#define WHITE       0
/* and a set of 8 more intermediate colors; names from the
    /usr/lib/X11/rgb.txt database */
#define AQUAMARINE  8
#define FORESTGREEN 9
#define ORANGE     10
#define VIOLET     11
#define BROWN      12
#define PINK       13
#define CORAL      14
#define GRAY       15

/* Had a "ifndef lint around this, but that is wrong for using gcc with
   string checking and -Dlint to remove the "vcid" not used */
static char *(colornames[]) = { "white", "black", "red", "yellow", "green", 
			      "cyan", "blue", "magenta", "aquamarine",
			      "forestgreen", "orange", "marroon", "brown",
			      "pink", "coral", "gray" };

/*I "xtools/baseclr.h" I*/

/*
      XBPixFromInteger - Looks up an entry in the win->cmapping

    Input Parameters:
     color - an integer color as listed in xtools/baseclr.h

    Synopsis:
    PixVal XBPixFromInteger(win,color)
    XBWindow *win;
    int      color; 
 */
#define XBPixFromInteger(win,color) (win)->cmapping[color]

/* Prototypes */
Colormap XBCreateColormap ( Display *, int, Visual * );
int XBHlsToRgb ( int, int, int, 
			  unsigned char *, unsigned char *, unsigned char * );

#endif

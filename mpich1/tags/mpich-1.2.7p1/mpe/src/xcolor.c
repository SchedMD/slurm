#include "mpeconf.h"
#include "mpetools.h"
#include "basex11.h"
#include "baseclr.h"

/* This is used to correct system header files without prototypes */
#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
    This file contains routines to provide color support where available.
    This is made difficult by the wide variety of color implementations
    that X11 supports, and the failure of the X consortium to recognize
    until very recently that window managers must have a minimum,
    standardized interface that includes color (and window mapping,
    mouse control, ...  )
 */
/* for some reason, this isn't included by the X11 includes */
#include <X11/Xatom.h>
int XBHlsHelper ( int, int, int );

/* cmap is ignored for now? */
void XBInitColors( XBWin, cmap, nc )
XBWindow *XBWin;
Colormap cmap;
int      nc;
{
    PixVal   white_pixel, black_pixel;

/* reset the number of colors from info on the display */
/* This is wrong; it needs to take the value from the visual */
/* Also, I'd like to be able to set this so as to force B&W behaviour
   on color displays */
    if (nc > 0) 
	XBWin->numcolors = nc;
    else
	XBWin->numcolors = 1 << DefaultDepth( XBWin->disp, XBWin->screen );

/* we will use the default colormap of the visual */
    if (!XBWin->cmap)
	XBWin->cmap = XBCreateColormap( XBWin->disp, XBWin->screen, XBWin->vis );

/* get the initial colormap */
    if (XBWin->numcolors > 2)
	XBInitCmap( XBWin );
    else {
	/* note that the 1-bit colormap is the DEFAULT map */
	white_pixel     = WhitePixel(XBWin->disp,XBWin->screen);
	black_pixel     = BlackPixel(XBWin->disp,XBWin->screen);
	/* the default "colormap"; the mapping from color indices to 
	   X pixel values */
	XBWin->cmapping[BLACK]   = black_pixel;
	XBWin->cmapping[WHITE]   = white_pixel;
	XBWin->foreground        = black_pixel;
	XBWin->background        = white_pixel;
    }
}


/*
    Set the initial color map
 */
int XBInitCmap( XBWin )
XBWindow *XBWin;
{
    XColor  colordef;
    int     i;
/* Also, allocate black and white first, in the same order that
   there "pixel" values are, incase the pixel values assigned
   start from 0 */
    XBAllocBW( XBWin, &XBWin->cmapping[WHITE], &XBWin->cmapping[BLACK] );
    XBWin->background = XBWin->cmapping[WHITE];
    XBWin->foreground = XBWin->cmapping[BLACK];
/* Look up the colors so that they can be use server standards
   (and be corrected for the monitor) */
    for (i=2; i<16; i++) {
	XParseColor( XBWin->disp, XBWin->cmap, colornames[i], &colordef );
	XAllocColor( XBWin->disp, XBWin->cmap, &colordef );
	XBWin->cmapping[i]   = colordef.pixel;
    }
    XBWin->maxcolors = 15;

    return 0;
}

/*
 * The input to this routine is RGB, not HLS.
 * X colors are 16 bits, not 8, so we have to shift the input by 8.
 */
int XBCmap( red, green, blue, mapsize, XBWin )
int           mapsize;
unsigned char red[], green[], blue[];
XBWindow      *XBWin;
{
    int         i, err;
    XColor      colordef;
    PixVal      white_pixel, black_pixel, pix, white_pix, black_pix;

    white_pixel     = WhitePixel(XBWin->disp,XBWin->screen);
    black_pixel     = BlackPixel(XBWin->disp,XBWin->screen);

/*
    Free the old colors if we have the colormap.
 */
    if (XBWin->cmap != DefaultColormap( XBWin->disp, XBWin->screen ) ) {
	if (XBGetVisualClass( XBWin ) == PseudoColor ||
	    XBGetVisualClass( XBWin ) == DirectColor )
	    XFreeColors( XBWin->disp, XBWin->cmap, XBWin->cmapping,
			 XBWin->maxcolors + 1, (unsigned long)0 );
    }

/*
   The sun convention is that 0 is the background and 2**depth-1 is
   foreground.  We make these the Xtools conventions (ignoring foreground)
  */

    if (mapsize > XBWin->numcolors) mapsize = XBWin->numcolors;

    XBWin->maxcolors = mapsize - 1;
/*  Now, set the color values

    Since it is hard (impossible?) to insure that black and white are
    allocated to the SAME pixel values in the default/window manager
    colormap, we ALWAYS allocate black and white FIRST

    Note that we may have allocated more than mapsize colors if the
    map did not include black or white.  We need to handle this later.
 */
    XBAllocBW( XBWin, &white_pix, &black_pix );
    err = 0;
    for (i=0; i<mapsize; i++) {
	if (red[i] == 0 && green[i] == 0 && blue[i] == 0)
	    XBWin->cmapping[i]   = black_pix;
	else if (red[i] == 255 && green[i] == 255 && blue[i] == 255)
	    XBWin->cmapping[i]   = white_pix;
	else {
	    colordef.red    = ((int)red[i]   * 65535) / 255;
	    colordef.green  = ((int)green[i] * 65535) / 255;
	    colordef.blue   = ((int)blue[i]  * 65535) / 255;
	    colordef.flags  = DoRed | DoGreen | DoBlue;
	    if (!XAllocColor( XBWin->disp, XBWin->cmap, &colordef ))
		err = 1;
	    XBWin->cmapping[i]   = colordef.pixel;
	}
	/* printf( "pixel value for %d is %d\n\r", i, XBWin->cmap[i] ); */
    }

/* make sure that there are 2 different colors */
    pix             = XBWin->cmapping[0];
    for (i=1; i<mapsize; i++)
	if (pix != XBWin->cmapping[i]) break;
    if (i >= mapsize) {
	/* no different colors */
	if (XBWin->cmapping[0] != black_pixel)
	    XBWin->cmapping[0]   = black_pixel;
	else
	    XBWin->cmapping[0]   = white_pixel;
    }

/*
    The window needs to be told the new background pixel so that things
    like XClearArea will work

    Note that this should not be called until the window is actually
    created.
 */
    if (XBWin->win)
	XSetWindowBackground( XBWin->disp, XBWin->win, XBWin->cmapping[0] );

/*
   Note that since we haven't allocated a range of pixel-values to this
   window, the changes will only take effect with future writes.
   Further, several colors may have been mapped to the same display color.
   We could detect this only by seeing if there are any duplications
   among the XBWin->cmap values.
 */

/* 
   Remaining bug: foreground and background not set.
 */
    return err;
}

/*
    Color in X is many-layered.  The first layer is the "visual", a
    immutable attribute of a window set when the window is
    created.

    The next layer is the colormap.  The installation of colormaps is
    the buisness of the window manager (in some distant later release).
    Rather than fight with that, we will use the default colormap.
    This usually does not have many (any?) sharable color entries,
    so we just try to match with the existing entries.
 */

/*
    This routine gets the visual class (PseudoColor, etc) and returns
    it.  It finds the default visual.  Possible returns are
	PseudoColor
	StaticColor
	DirectColor
	TrueColor
	GrayScale
	StaticGray
 */
int XBSetVisualClass( XBWin )
XBWindow *XBWin;
{
    XVisualInfo vinfo;
    if (XMatchVisualInfo( XBWin->disp, XBWin->screen, 
			  24, DirectColor, &vinfo)) {
	XBWin->vis    = vinfo.visual;
	return 0;
    }
    if (XMatchVisualInfo( XBWin->disp, XBWin->screen, 
			  8, PseudoColor, &vinfo)) {
	XBWin->vis    = vinfo.visual;
	return 0;
    }
    if (XMatchVisualInfo( XBWin->disp, XBWin->screen,
			  DefaultDepth(XBWin->disp,XBWin->screen), 
			  PseudoColor, &vinfo)) {
	XBWin->vis    = vinfo.visual;
	return 0;
    }
    XBWin->vis    = DefaultVisual( XBWin->disp, XBWin->screen );

    return 0;
}

int XBGetVisualClass( XBWin )
XBWindow *XBWin;
{
    return XBWin->vis->class;
}

/* Should pass this an XBWin */
Colormap XBCreateColormap( display, screen, visual )
Display *display;
int      screen;
Visual  *visual;
{
    Colormap Cmap;

    if (DefaultDepth( display, screen ) <= 1)
	Cmap    = DefaultColormap( display, screen );
    else
	Cmap    = XCreateColormap( display, RootWindow(display,screen),
				   visual, AllocNone );
    return Cmap;
}


int XBSetColormap( XBWin )
XBWindow *XBWin;
{
    XSetWindowColormap( XBWin->disp, XBWin->win, XBWin->cmap );
    return 0;
}


int XBAllocBW( XBWin, white, black )
XBWindow *XBWin;
PixVal   *white, *black;
{
    XColor  bcolor, wcolor;
    XParseColor( XBWin->disp, XBWin->cmap, "black", &bcolor );
    XParseColor( XBWin->disp, XBWin->cmap, "white", &wcolor );
    if (BlackPixel(XBWin->disp,XBWin->screen) == 0) {
	XAllocColor( XBWin->disp, XBWin->cmap, &bcolor );
	XAllocColor( XBWin->disp, XBWin->cmap, &wcolor );
    }
    else {
	XAllocColor( XBWin->disp, XBWin->cmap, &wcolor );
	XAllocColor( XBWin->disp, XBWin->cmap, &bcolor );
    }
    *black = bcolor.pixel;
    *white = wcolor.pixel;

    return 0;
}


int XBGetBaseColor( XBWin, white_pix, black_pix )
XBWindow *XBWin;
PixVal   *white_pix, *black_pix;
{
    *white_pix  = XBWin->cmapping[WHITE];
    *black_pix  = XBWin->cmapping[BLACK];

    return 0;
}

/*
    Set up a color map, using uniform separation in hue space.
    Map entries are Red, Green, Blue.
    Values are "gamma" corrected.
 */

/*  
   Gamma is a monitor dependent value.  The value here is an 
   approximate that gives somewhat better results than Gamma = 1.
 */
static double Gamma = 2.0;
#include <math.h>

int XBSetGamma( g )
double g;
{
    Gamma = g;

    return 0;
}

int XBSetCmapHue( red, green, blue, mapsize )
int             mapsize;
unsigned char   *red, *green, *blue;
{
    int     i, hue, lightness, saturation;
    double  igamma = 1.0 / Gamma;

    red[0]      = 0;
    green[0]    = 0;
    blue[0]     = 0;
    hue         = 0;        /* in 0:359 */
    lightness   = 50;       /* in 0:100 */
    saturation  = 100;      /* in 0:100 */
    for (i = 1; i < mapsize-1; i++) {
	XBHlsToRgb( hue, lightness, saturation, red + i, green + i, blue + i );
	red[i]   = floor( 255.999 * pow( ((double)  red[i])/255.0, igamma ) );
	blue[i]  = floor( 255.999 * pow( ((double) blue[i])/255.0, igamma ) );
	green[i] = floor( 255.999 * pow( ((double)green[i])/255.0, igamma ) );
	hue += (359/(mapsize-2));
    }
    red  [mapsize-1]    = 255;
    green[mapsize-1]    = 255;
    blue [mapsize-1]    = 255;

    return 0;
}

/*
 * This algorithm is from Foley and van Dam, page 616
 * given
 *   (0:359, 0:100, 0:100).
 *      h       l      s
 * set
 *   (0:255, 0:255, 0:255)
 *      r       g      b
 */
int XBHlsHelper( h, n1, n2 )
int     h, n1, n2;
{
while (h > 360) h = h - 360;
while (h < 0)   h = h + 360;
if (h < 60) return n1 + (n2-n1)*h/60;
if (h < 180) return n2;
if (h < 240) return n1 + (n2-n1)*(240-h)/60;
return n1;
}

int XBHlsToRgb( h, l, s, r, g, b )
int             h, l, s;
unsigned char   *r, *g, *b;
{
int m1, m2;         /* in 0 to 100 */
if (l <= 50) m2 = l * ( 100 + s ) / 100 ;           /* not sure of "/100" */
else         m2 = l + s - l*s/100;

m1  = 2*l - m2;
if (s == 0) {
    /* ignore h */
    *r  = 255 * l / 100;
    *g  = 255 * l / 100;
    *b  = 255 * l / 100;
    }
else {
    *r  = (255 * XBHlsHelper( h+120, m1, m2 ) ) / 100;
    *g  = (255 * XBHlsHelper( h, m1, m2 ) )     / 100;
    *b  = (255 * XBHlsHelper( h-120, m1, m2 ) ) / 100;
    }
return 0;
}


/*
    This routine returns the pixel value for the specified color
    Returns 0 on failure, <>0 otherwise.
 */
int XBFindColor( XBWin, name, pixval )
XBWindow *XBWin;
char     *name;
PixVal   *pixval;
{
    XColor   colordef;
    int      st;

    st = XParseColor( XBWin->disp, XBWin->cmap, name, &colordef );
    if (st) {
	st  = XAllocColor( XBWin->disp, XBWin->cmap, &colordef );
	if (st)
	    *pixval = colordef.pixel;
    }
    else
	printf( "did not find color %s\n", name );
    return st;
}

/*
    When there are several windows being displayed, it may help to
    merge their colormaps together so that all of the windows
    may be displayed simultaneously with true colors.
    These routines attempt to accomplish this
 */

/*
 * The input to this routine is RGB, not HLS.
 * X colors are 16 bits, not 8, so we have to shift the input by 8.
 * This is like XBCmap, except that it APPENDS to the existing
 * colormap.
 */
int XBAddCmap( red, green, blue, mapsize, XBWin )
int           mapsize;
unsigned char red[], green[], blue[];
XBWindow      *XBWin;
{
    int      i, err;
    XColor   colordef;
    int      cmap_start;

    if (mapsize + XBWin->maxcolors > XBWin->numcolors)
	mapsize = XBWin->numcolors - XBWin->maxcolors;

    cmap_start  = XBWin->maxcolors;
    XBWin->maxcolors += mapsize;

    err = 0;
    for (i=0; i<mapsize; i++) {
	colordef.red    = ((int)red[i]   * 65535) / 255;
	colordef.green  = ((int)green[i] * 65535) / 255;
	colordef.blue   = ((int)blue[i]  * 65535) / 255;
	colordef.flags  = DoRed | DoGreen | DoBlue;
	if (!XAllocColor( XBWin->disp, XBWin->cmap, &colordef ))
	    err = 1;
	XBWin->cmapping[cmap_start+i]    = colordef.pixel;
	/* printf( "pixel value for %d is %d\n\r", i, XBWin->cmapping[i] ); */
    }

    return err;
}


/*
    Another real need is to assign "colors" that make sense for
    a monochrome display, without unduely penalizing color displays.
    This routine takes a color name, a window, and a flag that
    indicates whether this is "background" or "foreground".
    In the monchrome case (or if the color is otherwise unavailable),
    the "background" or "foreground" colors will be chosen
 */
PixVal XBGetColor( XBWin, name, is_fore )
XBWindow *XBWin;
char     *name;
int      is_fore;
{
    PixVal pixval;
    if (XBWin->numcolors == 2 || !XBFindColor( XBWin, name, &pixval ))
	pixval  = is_fore ? XBWin->cmapping[WHITE] : XBWin->cmapping[BLACK];
    return pixval;
}

/*
   This routine takes a named color and returns a color that is either
   lighter or darker
 */
PixVal XBSimColor( XBWin, pixel, intensity, is_fore )
XBWindow *XBWin;
PixVal   pixel;
int      intensity, is_fore;
{
    XColor   colordef, colorsdef;
    char     RGBcolor[20];
    PixVal   red, green, blue;
    /*  int      st;  */

    colordef.pixel = pixel;
    XQueryColor( XBWin->disp, XBWin->cmap, &colordef );
/* Adjust the color value up or down.  Get the RGB values for the color */
    red   = colordef.red;
    green = colordef.green;
    blue  = colordef.blue;
#define min(a,b) ((a)<(b) ? a : b)
#define max(a,b) ((a)>(b) ? a : b)
#define WHITE_AMOUNT 5000
    if (intensity > 0) {
	/* Add white to the color */
	red   = min(65535,red + WHITE_AMOUNT);
	green = min(65535,green + WHITE_AMOUNT);
	blue  = min(65535,blue + WHITE_AMOUNT);
    }
    else {
	/* Subtract white from the color */
	red   = (red   < WHITE_AMOUNT) ? 0 : red - WHITE_AMOUNT;
	green = (green < WHITE_AMOUNT) ? 0 : green - WHITE_AMOUNT;
	blue  = (blue  < WHITE_AMOUNT) ? 0 : blue - WHITE_AMOUNT;
    }
    sprintf( RGBcolor, "rgb:%4.4x/%4.4x/%4.4x", (unsigned int)red, 
	     (unsigned int) green, (unsigned int) blue );
    XLookupColor( XBWin->disp, XBWin->cmap, RGBcolor, &colordef, &colorsdef );
    /*
    st = XLookupColor( XBWin->disp, XBWin->cmap, RGBcolor, &colordef, 
		       &colorsdef );
    */    
    return  colorsdef.pixel;
}


/* 
  XBUniformHues - Set the colormap to a uniform distribution

  Input parameters:
. XBwin - window to set colors for
. ncolors - number of colors

  Note:
  This routine sets the colors in the current colormap, if the default
  colormap is used.  The Pixel values chosen are in the cmapping 
  structure; this is used by routines such as the XB contour plotter.
  */  
void XBUniformHues( XBwin, ncolors )
XBWindow *XBwin;
int      ncolors;
{
    unsigned char *red, *green, *blue;

    red   = (unsigned char *)MALLOC( 3 * ncolors * sizeof(unsigned char) );   
    /* CHKPTR(red); */
    green = red + ncolors;
    blue  = green + ncolors;
    XBSetCmapHue( red, green, blue, ncolors );
    XBCmap( red, green, blue, ncolors, XBwin );
    FREE( red );
}

/* 
  XBSetCmapLight - Create rgb values from a single color by adding white
  
  Input Parameters:
. mapsize - number of values

  Output Parameters:
. red - red values
. green - green values
. blue - blue values

  Note:
  The initial color is (red[0],green[0],blue[0]).
  */
void XBSetCmapLight( red, green, blue, mapsize )
     int             mapsize;
     unsigned char   *red, *green, *blue;
{
  int     i ;

  for (i = 1; i < mapsize-1; i++) 
    {
      blue[i]  = i*(255-(int)blue[0])/(mapsize-2)+(int)blue[0] ;
      green[i] = i*(255-(int)green[0])/(mapsize-2)+(int)green[0] ;
      red[i]   = i*(255-(int)red[0])/(mapsize-2)+(int)red[0] ;
    }
  red[mapsize-1] = green[mapsize-1] = blue[mapsize-1] = 255;
}

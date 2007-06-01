
/*
   This file contains routines to draw a 3-d like frame about a given 
   box with a given width.  Note that we might like to use a high/low
   color for highlights.

   The region has 6 parameters.  These are the dimensions of the actual frame.
 */

#include "mpeconf.h"
#include "mpetools.h"
#include "basex11.h"


/* 50% grey stipple pattern */
static Pixmap grey50 = (Pixmap)0;         
#define cboard50_width 8
#define cboard50_height 8
static unsigned char cboard50_bits[] = {
   0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa};

static PixVal HiPix=0, LoPix=0;
/* 
   Set the colors for the highlights by name 
 */
int XBFrameColors( XBWin, Rgn, Hi, Lo )
XBWindow     *XBWin;
XBDecoration *Rgn;
char         *Hi, *Lo;
{
    Rgn->Hi = XBGetColor( XBWin, Hi, 1 );
    Rgn->Lo = XBGetColor( XBWin, Lo, 1 );
    Rgn->HasColor = Rgn->Hi != Rgn->Lo;

    return 0;
}

int XBDrawFrame( XBWin, Rgn )
XBWindow *XBWin;
XBDecoration *Rgn;
{
    int    xl = Rgn->Box.x, yl = Rgn->Box.y, 
	xh = Rgn->Box.xh, yh = Rgn->Box.yh,
	o = Rgn->width;
    XPoint high[7], low[7];
    PixVal Hi, Lo;

/* High polygon */
    high[0].x = xl;            high[0].y = yh;
    high[1].x = xl + o;        high[1].y = yh - o;
    high[2].x = xh - o;        high[2].y = yh - o;
    high[3].x = xh - o;        high[3].y = yl + o;
    high[4].x = xh;            high[4].y = yl;
    high[5].x = xh;            high[5].y = yh;
    high[6].x = xl;            high[6].y = yh;     /* close path */

    low[0].x  = xl;            low[0].y = yh;
    low[1].x  = xl;            low[1].y = yl;
    low[2].x  = xh;            low[2].y = yl;
    low[3].x  = xh - o;        low[3].y = yl + o;
    low[4].x  = xl + o;        low[4].y = yl + o;
    low[5].x  = xl + o;        low[5].y = yh - o;
    low[6].x  = xl;            low[6].y = yh;      /* close path */

    if (Rgn->HasColor) {
	if (Rgn->Hi) Hi = Rgn->Hi;
	else         Hi = HiPix;
	if (Rgn->Lo) Lo = Rgn->Lo;
	else         Lo = LoPix;
	XBSetPixVal( XBWin, Rgn->is_in ? Hi : Lo );
	if ( o <= 1 )
	    XDrawLines( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
			high, 7, CoordModeOrigin );
	else
	    XFillPolygon( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
			  high, 7, Nonconvex, CoordModeOrigin);
	XBSetPixVal( XBWin, Rgn->is_in ? Lo : Hi );
	if ( o <= 1 )
	    XDrawLines( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
			low, 7, CoordModeOrigin );
	else
	    XFillPolygon( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
			  low, 7, Nonconvex, CoordModeOrigin);
	/* We could use additional highlights here, such as lines drawn
	   connecting the mitred edges. */		 
    }
    else {
	if (!grey50) 
	    grey50 = XCreatePixmapFromBitmapData(XBWin->disp, XBWin->win, 
						 (char *)cboard50_bits,
						 cboard50_width, 
						 cboard50_height, 1, 0, 1);
	XBSetPixVal( XBWin, Rgn->Hi );
	XFillPolygon( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
		      high, 7, Nonconvex, CoordModeOrigin);
	/* This can actually be done by using a stipple effect */
	XSetFillStyle( XBWin->disp, XBWin->gc.set, FillStippled );
	XSetStipple( XBWin->disp, XBWin->gc.set, grey50 );
	XFillPolygon( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
		      low, 7, Nonconvex, CoordModeOrigin);
	XSetFillStyle( XBWin->disp, XBWin->gc.set, FillSolid );
    }
    return 0;
}

/*
    XBClearWindow - Clear a region in a window

    Input parameters:
.   XBWin - window
.   x,y   - upper left corner of region to clear
.   w,h   - width and height of region to clear
*/
void XBClearWindow( XBWin, x, y, w, h )
XBWindow *XBWin;
int      x, y, w, h;
{
    XBSetPixVal(XBWin, XBWin->background );
    XFillRectangle( XBWin->disp, XBDrawable(XBWin), XBWin->gc.set, 
		    x, y, w, h );
}

/*
   Set the colors for the highlights by name 
 */
void XBFrameColorsByName( XBWin, Hi, Lo )
XBWindow *XBWin;
char     *Hi, *Lo;
{
    if (XBWin->numcolors > 2) {
	HiPix = XBGetColor( XBWin, Hi, 1 );
	LoPix = XBGetColor( XBWin, Lo, 1 );
	/* HasColor = 1; */
    }
}

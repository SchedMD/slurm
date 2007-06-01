/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import java.awt.Cursor;
import java.awt.Toolkit;
import java.awt.Image;
import java.awt.Point;
import java.awt.Dimension;
import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.net.URL;
import javax.swing.ImageIcon;

public class CustomCursor
{
    public  static Cursor   Normal       = null;
    public  static Cursor   Wait         = null;
    public  static Cursor   Hand         = null;
    public  static Cursor   HandOpen     = null;
    public  static Cursor   HandClose    = null;
    public  static Cursor   ZoomPlus     = null;
    public  static Cursor   ZoomMinus    = null;

    private static Toolkit  toolkit      = null;

    static {
        ( new CustomCursor() ).initCursors();
    }

    // private static URL getURL( String filename )
    private URL getURL( String filename )
    {
        // return ClassLoader.getSystemResource( Const.IMG_PATH + filename );
        return getClass().getResource( Const.IMG_PATH + filename );
    }

    private Image getBestCursorImage( String filename )
    {
        URL            icon_URL;
        Image          img;
        Dimension      opt_size;
        Graphics2D     g2d;
        int            iwidth, iheight;

        icon_URL = getURL( filename );
        img      = new ImageIcon( icon_URL ).getImage();
        iwidth   = img.getWidth( null );
        iheight  = img.getHeight( null );
        opt_size = toolkit.getBestCursorSize( iwidth, iheight );
        if ( opt_size.width == iwidth && opt_size.height == iheight )
            return img;
        else {
            BufferedImage  buf_img;
            buf_img = new BufferedImage( opt_size.width, opt_size.height,
                                          BufferedImage.TYPE_INT_ARGB );
            System.out.println( filename
                              + ": (" + iwidth + "," + iheight + ") -> ("
                              + opt_size.width + "," + opt_size.height + ")" );
            g2d     = buf_img.createGraphics();
            g2d.drawImage( img, 0, 0, null );
            g2d.dispose();
            return buf_img;
        }
    }

    public void initCursors()
    {
        Image    img;
        Point    pt;

        Normal   = Cursor.getPredefinedCursor( Cursor.DEFAULT_CURSOR );
        Wait     = Cursor.getPredefinedCursor( Cursor.WAIT_CURSOR );
        Hand     = Cursor.getPredefinedCursor( Cursor.HAND_CURSOR );

        toolkit   = Toolkit.getDefaultToolkit();
        pt        = new Point( 1, 1 );

        img       = this.getBestCursorImage( "HandOpenUpLeft25.gif" );
        HandOpen  = toolkit.createCustomCursor( img, pt, "Hand Open" );
        img       = this.getBestCursorImage( "HandCloseUpLeft25.gif" );
        HandClose = toolkit.createCustomCursor( img, pt, "Hand Close" );
        img       = this.getBestCursorImage( "ZoomPlusUpLeft25.gif" );
        ZoomPlus  = toolkit.createCustomCursor( img, pt, "Zoom Plus" );
        img       = this.getBestCursorImage( "ZoomMinusUpLeft25.gif" );
        ZoomMinus = toolkit.createCustomCursor( img, pt, "Zoom Minus" );
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import java.awt.Font;
import java.awt.Color;
import java.awt.RenderingHints;

public class Const
{
    public  static final int     FONT_SIZE = 10;
    public  static final Font    FONT      = new Font( "SansSerif",
                                                       Font.PLAIN, FONT_SIZE );

    public  static final int     MIN_ZOOM_LEVEL         = 0;
    public  static final int     MAX_ZOOM_LEVEL         = 30;

    public  static final String  RULER_TIME_FORMAT      = "#,##0.00######";
    public  static final String  PANEL_TIME_FORMAT      = "#,##0.00########";
    public  static final String  INFOBOX_TIME_FORMAT    = "#,##0.000000####";
    public  static final String  INTEGER_FORMAT         = "#,##0";
    public  static final String  FLOAT_FORMAT           = "0.0##";
    public  static final String  SHORT_FORMAT           = "##0";
    public  static final String  STRING_FORMAT          = null;
    public  static final String  BOOLEAN_FORMAT         = null;

    public  static final int     LABEL_INDENTATION      = 5;
    public  static final int     TIME_SCROLL_UNIT_INIT  = 10;

    public  static final Alias   ANTIALIAS_DEFAULT
            = new Alias( RenderingHints.VALUE_ANTIALIAS_DEFAULT, "default" );
    public  static final Alias   ANTIALIAS_OFF
            = new Alias( RenderingHints.VALUE_ANTIALIAS_OFF, "off" );
    public static final  Alias   ANTIALIAS_ON
            = new Alias( RenderingHints.VALUE_ANTIALIAS_ON, "on" );

    public  static final Alias   COLOR_BLACK
            = new Alias( Color.black, "black" );
    public  static final Alias   COLOR_DARKGRAY
            = new Alias( Color.darkGray, "dark gray" );
    public  static final Alias   COLOR_GRAY
            = new Alias( Color.gray, "gray" );
    public  static final Alias   COLOR_LIGHTGRAY
            = new Alias( Color.lightGray, "light gray" );
    public  static final Alias   COLOR_WHITE
            = new Alias( Color.white, "white" );

    public  static final String  IMG_PATH = "/images/";
    public  static final String  DOC_PATH = "/html/";

    public static Alias  parseAntiAliasing( String str_rep )
    {
        if ( str_rep.equalsIgnoreCase( ANTIALIAS_DEFAULT.toString() ) )
            return ANTIALIAS_DEFAULT;
        else if ( str_rep.equalsIgnoreCase( ANTIALIAS_OFF.toString() ) )
            return ANTIALIAS_OFF;
        else if ( str_rep.equalsIgnoreCase( ANTIALIAS_ON.toString() ) )
            return ANTIALIAS_ON;
        return ANTIALIAS_DEFAULT;
    }

    public static Alias  parseBackgroundColor( String str_rep )
    {
        if ( str_rep.equalsIgnoreCase( COLOR_BLACK.toString() ) )
            return COLOR_BLACK;
        else if ( str_rep.equalsIgnoreCase( COLOR_DARKGRAY.toString() ) )
            return COLOR_DARKGRAY;
        else if ( str_rep.equalsIgnoreCase( COLOR_GRAY.toString() ) )
            return COLOR_GRAY;
        else if ( str_rep.equalsIgnoreCase( COLOR_LIGHTGRAY.toString() ) )
            return COLOR_LIGHTGRAY;
        else if ( str_rep.equalsIgnoreCase( COLOR_WHITE.toString() ) )
            return COLOR_WHITE;
        return COLOR_BLACK;
    }
}

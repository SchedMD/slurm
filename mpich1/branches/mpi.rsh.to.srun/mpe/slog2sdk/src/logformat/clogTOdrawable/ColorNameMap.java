/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clogTOdrawable;

import java.util.Map;
import java.util.HashMap;
import java.util.Iterator;
import java.util.StringTokenizer;
import java.net.URL;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;

import base.drawable.ColorAlpha;

public class ColorNameMap
{
    private static HashMap    colormap = null;

    public static void initMapFromRGBtxt( String filename )
    {
        InputStream        ins;
        InputStreamReader  insrdr;
        BufferedReader     bufrdr;
        StringTokenizer    tokens;
        String             line;
        String             colorname;
        ColorAlpha         aRGB;
        int                red, green, blue;

        ins      = ClassLoader.getSystemResourceAsStream( filename );
        if ( ins == null ) {
            System.err.println( "ColorNameMap: Could NOT locate "
                              + filename + " in CLASSPATH.  Exiting...!" );
            System.exit( 1 );
        }
        colormap = new HashMap( 800 );

        try {
            insrdr = new InputStreamReader( ins );
            bufrdr = new BufferedReader( insrdr, 20480 );

            while ( ( line = bufrdr.readLine() ) != null ) {
                tokens     = new StringTokenizer( line );
                red        = Integer.parseInt( tokens.nextToken() );
                green      = Integer.parseInt( tokens.nextToken() );
                blue       = Integer.parseInt( tokens.nextToken() );
                aRGB       = new ColorAlpha( red, green, blue );
                colorname  = tokens.nextToken();
                while ( tokens.hasMoreTokens() )
                    colorname += " " + tokens.nextToken();
                colormap.put( colorname, aRGB );
            }
        } catch ( java.io.IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }
    }

    //  Special interface for CLOG's StrCname which has ":"!
    public static ColorAlpha getColorAlpha( String in_colorname )
    {
        ColorAlpha  color;
        String      colorname;

        int  len      = in_colorname.indexOf( ':' );
        if ( len != -1 )
            colorname = in_colorname.substring( 0, len );
        else
            colorname = in_colorname;
        color = (ColorAlpha) colormap.get( colorname );
        if ( color == null )
            System.err.println( "ColorNameMap: Could NOT locate colorname "
                              + colorname + " in default rgb.txt, i.e. "
                              + "jumpshot.colors" ); 
        return color;
    }

    public static String getString()
    {
        StringBuffer rep = new StringBuffer( "ColorNameMap : \n" );
        Map.Entry    entry;
        int          idx = 0;
        Iterator entries = colormap.entrySet().iterator();
        while ( entries.hasNext() ) {
            idx++;
            entry = (Map.Entry) entries.next();
            rep.append( idx + ", " + (String) entry.getKey()
                      + " -> " + (ColorAlpha) entry.getValue() + "\n" );
        }
        return rep.toString();
    }

    public static final void main( String[] args )
    {
        String        filename = "jumpshot.colors";

        ColorNameMap.initMapFromRGBtxt( filename );
        // System.out.println( ColorNameMap.getString() );
        System.out.println( "navy blue is "
                          + ColorNameMap.getColorAlpha( "navy blue" ) );
        System.out.println( "green:dimple3 is "
                          + ColorNameMap.getColorAlpha( "green:dimple3" ) );
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.io.*;
import javax.swing.*;

public class Diagnosis
{
    private static final String  UnitIndentStr    = "    ";

    private boolean       isOn             = false;
    private String        filename         = null;
    private int           ilevel           = 0;
    private boolean       isLineHead       = true;
    private StringBuffer  strbuf           = null;

    private JTextArea     text_area        = null;

    public Diagnosis()
    {
        isOn             = false;
        filename         = null;
        ilevel           = 0;
        isLineHead       = true;
        strbuf           = new StringBuffer();
        text_area        = null;
    }

    public void initTextArea( String title_str )
    {
        if ( ! isOn )
            return;
        /*
          Bug in JTextArea ?  without specified Nrow and Ncolumn,
          the vertical scrollbar does NOT show up.  For example,
          the following does NOT work.

          text_area = new JTextArea();
          text_area.setPreferredSize( new Dimension( 256,128 ) );
        */
                            text_area = new JTextArea( 10, 30 );
            JScrollPane text_scroller = new JScrollPane( text_area );

        JFrame text_frame = new JFrame( title_str );
        text_frame.getContentPane().add( text_scroller );
        text_frame.pack();
        text_frame.setVisible( true );
    }

    public void setActive( boolean is_active )
    {
        isOn = is_active;
    }

    public boolean isActive()
    {
        return isOn;
    }

    public void setFilename( String in_name )
    throws IOException
    {
        filename = new String( in_name );
        System.setOut( new PrintStream( new FileOutputStream(in_name) ) );
    }

    /* 
       print() is used as StringBuffer.append()
       and not flushed out till println() is called.
    */
    public void print( String str )
    {
        // if ( isOn ) {
            if ( str.indexOf( "END" ) > 0 )
                ilevel -= 1;

            if ( isLineHead )
                for ( int ii = ilevel; ii > 0; ii -= 1 )
                    strbuf.append( UnitIndentStr );
            strbuf.append( str );
            isLineHead = false;

            if ( str.indexOf( "START" ) > 0 )
                ilevel += 1;
        // }
    }

    public void println( String str )
    {
        // if ( isOn ) {
            if ( str.indexOf( "END" ) > 0 )
                ilevel -= 1;

            if ( isLineHead )
                for ( int ii = ilevel; ii > 0; ii -= 1 )
                   strbuf.append( UnitIndentStr );
            strbuf.append( str );
            strbuf.append( "\n" );
            isLineHead = true;

            if ( str.indexOf( "START" ) > 0 )
                ilevel += 1;

            if ( ilevel == 0 ) {
                if ( text_area == null ) {
                    System.out.print( strbuf.toString() );
                    System.out.flush();
                }
                else {
                    text_area.append( strbuf.toString() );
                    text_area.setCaretPosition(
                              text_area.getDocument().getLength() );
                }
                strbuf = new StringBuffer();
            }
        // }
    }
}

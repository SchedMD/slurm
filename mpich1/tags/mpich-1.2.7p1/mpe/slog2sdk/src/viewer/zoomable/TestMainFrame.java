/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

public class TestMainFrame extends JFrame
{
    private TestMainPanel   top_panel;

    public TestMainFrame()
    {
        super( "main" );
        top_panel = new TestMainPanel( this );
        setContentPane( top_panel );

        addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent e ) {
                System.exit( 0 );
            }
        } );

        setVisible( true );
        // pack();
    }

    public void init()
    {
        top_panel.init();
    }

    public static void checkVersion()
    {
        String vers = System.getProperty ("java.version");
        System.out.println ( "Java is version " + vers + "." );
        if ( vers.compareTo( "1.2.0" ) < 0 )
            System.err.println ( "WARNING: Java is version " + vers + ". \n" +
                                 "\t It is too old to run this prototype." );
    }

    public static void main( String[] args )
    {
        TestMainFrame      frame;

        checkVersion();
        // Debug.setActive( true ); Debug.initTextArea();
        Debug.setActive( true ); Debug.initTextArea();

        frame     = new TestMainFrame();
        // frame.setSize( new Dimension( 460, 100 ) );
        // frame.pack() has to be called after the object is created
        frame.pack();
        frame.init();
    }

    private static String indexOrderStr( int idx )
    {
        switch (idx) {
            case 1  : return Integer.toString( idx ) + "st";
            case 2  : return Integer.toString( idx ) + "nd";
            case 3  : return Integer.toString( idx ) + "rd";
            default : return Integer.toString( idx ) + "th";
        }
    }
}

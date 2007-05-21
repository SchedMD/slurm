/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.first;

import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

// import java.util.Properties;
import java.util.Enumeration;

import viewer.common.Dialogs;
import viewer.common.TopWindow;
import viewer.common.TopControl;

public class FirstFrame extends JFrame
                        implements TopControl
{
    private static boolean        isApplet = false;
    private static String         filename = null;
    private static int            view_ID  = -1;

    private        FirstPanel     top_panel;
    private        FirstMenuBar   top_menubar;

    public FirstFrame()
    {
        super( "Jumpshot-4" );
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );
        TopWindow.First.setWindow( this );

        top_panel    = new FirstPanel( isApplet, filename, view_ID );
        super.setContentPane( top_panel );
        top_menubar  = new FirstMenuBar( isApplet, top_panel );
        super.setJMenuBar( top_menubar );

        /*
        Properties   sys_pptys;
        sys_pptys = System.getProperties();

        Enumeration  ppty_keys;
        String       ppty_key, ppty_val;
        ppty_keys = sys_pptys.propertyNames();
        while ( ppty_keys.hasMoreElements() ) {
            ppty_key  = (String) ppty_keys.nextElement();
            ppty_val  = sys_pptys.getProperty( ppty_key );
            System.out.println( ppty_key + " = " + ppty_val );
        }
        */

        addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent e ) {
                if ( Dialogs.confirm( TopWindow.First.getWindow(),
                     "Are you sure you want to exit Jumpshot-4 ?" ) ) {
                    TopWindow.First.disposeAll();
                }
            }
        } );

        // setVisible( true );
    }

    public void init()
    {
        top_panel.init();
    }

    public void setEditPreferenceButtonEnabled( boolean val )
    {
        top_panel.getEditPreferenceButton().setEnabled( val );
    }

    public void setShowLegendButtonEnabled( boolean val )
    {
        top_panel.getShowLegendButton().setEnabled( val );
    }

    public void setShowTimelineButtonEnabled( boolean val )
    {
        top_panel.getShowTimelineButton().setEnabled( val );
    }

    public static void checkVersion()
    {
        String vers = System.getProperty( "java.version" );
        System.out.println( "Java is version " + vers + "." );
        if ( vers.compareTo( "1.2.0" ) < 0 )
            System.err.println ( "WARNING: Java is version " + vers + ". \n" +
                                 "\t It is too old to run this viewer." );
    }

    public static void main( String[] args )
    {
        FirstFrame     frame;

        checkVersion();
        parseCmdLineArgs( args );

        viewer.zoomable.Debug.initTextArea();
        // viewer.zoomable.Profile.initTextArea();

        System.out.println( "Starting the SLOG-2 Display Program ..... " );
        frame     = new FirstFrame();
        // frame.setSize( new Dimension( 460, 100 ) );
        // frame.pack() has to be called after the object is created
        frame.pack();
        TopWindow.layoutIdealLocations();
        frame.setVisible( true );
        frame.init();
    }

    private static String help_msg = "Usage: java timelines.MainFrame "
                                   + "[options] [slog2_filename]\n"
                                   + "Options: \n"
                                   + "\t [-h|-help|--help]                 "
                                   + "\t Display this message.\n"
                                   + "\t [-debug]                          "
                                   + "\t Turn on Debugging output\n"
                                   + "\t [-profile]                        "
                                   + "\t Turn on Profiling output\n"
                                   + "\t [-v view_ID ]                     "
                                   + "\t Default value is -1.\n" ;

    private static void parseCmdLineArgs( String argv[] )
    {
        String        arg_str;
        StringBuffer  err_msg = new StringBuffer();
        int idx = 0;
        try {  // Unnecessary try block
            while ( idx < argv.length ) {
                if ( argv[ idx ].startsWith( "-" ) ) {
                    if (  argv[ idx ].equals( "-h" )
                       || argv[ idx ].equals( "-help" )
                       || argv[ idx ].equals( "--help" ) ) {
                        System.out.println( help_msg );
                        System.out.flush();
                        System.exit( 0 );
                    }
                    else if ( argv[ idx ].equals( "-v" ) ) {
                        arg_str = argv[ ++idx ];
                        view_ID = Integer.parseInt( arg_str );
                        err_msg.append( "\n view_ID = " + arg_str );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-debug" ) ) {
                        viewer.zoomable.Debug.setActive( true );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-profile" ) ) {
                        viewer.zoomable.Profile.setActive( true );
                        idx++;
                    }
                    else {
                        System.err.println( "Unrecognized option, "
                                          + argv[ idx ] + ", at "
                                          + indexOrderStr( idx+1 )
                                          + " command line argument" );
                        System.out.flush();
                        System.exit( 1 );
                    }
                }
                else {
                    filename   = argv[ idx ];
                    idx++;
                }
            }
        } catch ( NumberFormatException numerr ) {  // Not needed at this moment
            if ( err_msg.length() > 0 )
                System.err.println( err_msg.toString() );
            String idx_order_str = indexOrderStr( idx );
            System.err.println( "Error occurs after option "
                              + argv[ idx-1 ] + ", " + indexOrderStr( idx )
                              + " command line argument.  It needs a number." );            // System.err.println( help_msg );
            numerr.printStackTrace();
        }
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

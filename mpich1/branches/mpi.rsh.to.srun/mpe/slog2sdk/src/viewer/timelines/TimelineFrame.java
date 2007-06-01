/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

import logformat.slog2.input.InputLog;
import viewer.common.Routines;
import viewer.common.Dialogs;
import viewer.common.TopWindow;
import viewer.common.Parameters;

public class TimelineFrame extends JFrame
{
    private static String         in_filename;      // For main()
    private static int            in_view_ID  = 0;  // For main()

    private        TimelinePanel  top_panel;

    public TimelineFrame( final InputLog slog_ins, int view_ID )
    {
        super( "TimeLine : " + slog_ins.getPathnameSuffix()
             + "  <" + slog_ins.getLineIDMapName( view_ID ) + ">" );
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );
        TopWindow.Timeline.disposeAll();
        TopWindow.Timeline.setWindow( this );

        top_panel = new TimelinePanel( this, slog_ins, view_ID );
        setContentPane( top_panel );

        addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent e ) {
                TopWindow.Timeline.disposeAll();
            }
        } );

        /* setVisible( true ); */
    }

    public void setVisible( boolean val )
    {
        super.setVisible( val );
        TopWindow.Control.setShowTimelineButtonEnabled( !val );
    }

    public void init()
    {
        top_panel.init();
        // viewer.common.Routines.listAllComponents( this, 0 );
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
        InputLog       in_slog_ins;
        TimelineFrame  frame;
        String         err_msg;

        checkVersion();
        parseCmdLineArgs( args );

        // Debug.setActive( true ); Debug.initTextArea();

        System.out.print( "Reading the SLOG-2 file ...... " );
        in_slog_ins  = new InputLog( in_filename );
        if ( in_slog_ins == null ) {
            Dialogs.error( TopWindow.First.getWindow(), "Null InputLog!" );
            System.exit( 1 );
        }
        if ( ! in_slog_ins.isSLOG2() ) {
            Dialogs.error( TopWindow.First.getWindow(),
                           in_filename + " is NOT a SLOG-2 file!" );
            in_slog_ins = null;
            System.exit( 1 );
        }
        if ( (err_msg = in_slog_ins.getCompatibleHeader() ) != null ) {
            if ( ! Dialogs.confirm( TopWindow.First.getWindow(),
                            err_msg
                          + "Check the following version history "
                          + "for compatibility.\n\n"
                          + logformat.slog2.Const.VERSION_HISTORY + "\n"
                          + "Do you still want to continue reading "
                          + "the logfile ?" ) ) {
                 in_slog_ins = null;
                 System.exit( 1 );
            }
        }
        in_slog_ins.initialize();
        System.out.println( "Done." );

        /*  Initialization  */
        Parameters.initSetupFile();
        Parameters.readFromSetupFile( null );
        Parameters.initStaticClasses();

        System.out.println( "Starting the SLOG-2 Display Program ..... " );
        frame     = new TimelineFrame( in_slog_ins, in_view_ID );
        // frame.setSize( new Dimension( 460, 100 ) );
        // frame.pack() has to be called after the object is created
        frame.pack();
        frame.setVisible( true );
        frame.init();
    }

    private static String help_msg = "Usage: "
                                   + "java viewer.timelines.TimelineFrame "
                                   + "[options] slog2_filename.\n"
                                   + "Options: \n"
                                   + "\t [-h|-help|--help]                 "
                                   + "\t Display this message.\n"
                                   + "\t [-v view_ID ]                     "
                                   + "\t Default value is 0.\n" ;

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
                        in_view_ID = Integer.parseInt( arg_str );
                        err_msg.append( "\n view_ID = " + arg_str );
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
                    in_filename   = argv[ idx ];
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

        if ( in_filename == null ) {
            System.err.println( "The Program needs a SLOG-2 filename as "
                              + "a command line argument." );
            System.err.println( help_msg );
            System.exit( 1 );
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

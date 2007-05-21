/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.WindowEvent;
import java.awt.event.WindowAdapter;
import javax.swing.JFrame;
import javax.swing.WindowConstants;



public class ConvertorFrame extends JFrame
{
    private static String          in_filename;      // For main()

    private        ConvertorPanel  top_panel;

    public ConvertorFrame()
    {
        super( "Logfile Convertor" );
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );

        top_panel = new ConvertorPanel( null );
        top_panel.addActionListenerForOkayButton( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                ConvertorFrame.this.setVisible( false );
                ConvertorFrame.this.dispose();
                System.exit( 0 );
            }
        } );
        top_panel.addActionListenerForCancelButton( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                ConvertorFrame.this.setVisible( false );
                ConvertorFrame.this.dispose();
                System.exit( 0 );
            }
        } );
        super.setContentPane( top_panel );

        super.addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent evt ) {
                ConvertorFrame.this.setVisible( false );
                ConvertorFrame.this.dispose();
                System.exit( 0 );
            }
        } );

        /* setVisible( true ) */;
    }

    public void init( String trace_filename )
    {
        top_panel.init( trace_filename );
    }

    public static void main( String[] args )
    {
        ConvertorFrame    frame;

        checkVersion();
        parseCmdLineArgs( args );

        frame     = new ConvertorFrame();
        // frame.pack() has to be called after the object is created
        frame.pack();
        frame.setVisible( true );
        frame.init( in_filename );
    }

    public static void checkVersion()
    {
        String vers = System.getProperty( "java.version" );
        System.out.println( "Java is version " + vers + "." );
        if ( vers.compareTo( "1.2.0" ) < 0 )
            System.err.println ( "WARNING: Java is version " + vers + ". \n" +
                                 "\t It is too old to run this prototype." );
    }

    private static String help_msg = "Usage: "
                                   + "java viewer.convertor.ConvertorFrame "
                                   + "[options] trace_filename.\n"
                                   + "Options: \n"
                                   + "\t [-h|-help|--help]                 "
                                   + "\t Display this message.\n";

    private static void parseCmdLineArgs( String[] argv )
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
                    in_filename  = argv[ idx ];
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

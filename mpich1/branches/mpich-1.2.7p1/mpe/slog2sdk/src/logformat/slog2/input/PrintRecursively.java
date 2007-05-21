/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.input;

import base.drawable.*;
import logformat.slog2.*;
import logformat.slog2.input.InputLog;



public class PrintRecursively
{
    private static       boolean  printNode         = true;
    private static       String   in_filename;
    private static       double   time_init_ftr     = 0.0;
    private static       double   time_final_ftr    = 1.0;
    private static       short    lowest_depth      = 0;

    public static final void main( String[] args )
    {
        InputLog         slog_ins;
        CategoryMap      objdefs;
        TreeTrunk        treetrunk;
        TreeNode         treeroot;
        TimeBoundingBox  timebounds;
        String           err_msg;

        parseCmdLineArgs( args );

        slog_ins   = new InputLog( in_filename );
        if ( slog_ins == null ) {
            System.err.println( "Null input logfile!" );
            System.exit( 1 );
        }
        if ( ! slog_ins.isSLOG2() ) {
            System.err.println( in_filename + " is NOT SLOG-2 file!." );
            System.exit( 1 );
        }
        if ( (err_msg = slog_ins.getCompatibleHeader()) != null ) {
            System.err.print( err_msg );
            InputLog.stdoutConfirmation();
        }
        slog_ins.initialize();
        System.out.println( slog_ins );

        treetrunk  = new TreeTrunk( slog_ins, Drawable.INCRE_STARTTIME_ORDER );
        treetrunk.initFromTreeTop();
        treeroot   = treetrunk.getTreeRoot();
        if ( treeroot == null ) {
            System.out.println( "SLOG-2 file, " + in_filename + " "
                              + "contains no drawables" );
            slog_ins.close();
            System.exit( 0 );
        }

        timebounds = new TimeBoundingBox( treeroot );
        resetTimeBounds( timebounds );
        System.err.println( "Time Window is " + timebounds );

        treetrunk.growInTreeWindow( treeroot, lowest_depth, timebounds );
        if ( printNode )
            System.out.println( treetrunk.toString() );
        else
            System.out.println( treetrunk.toString( timebounds ) );
        
        slog_ins.close();
    }

    private static void resetTimeBounds( TimeBoundingBox endtimes )
    {
        double time_init  = endtimes.getEarliestTime();
        double time_final = endtimes.getLatestTime();
        double time_range = time_final - time_init;
        endtimes.setEarliestTime( time_init + time_init_ftr * time_range );
        endtimes.setLatestTime( time_init + time_final_ftr * time_range );
    }

    private static String help_msg = "Usage: java slog2.input.Print "
                                   + "[options] slog2_filename.\n"
                                   + "Options: \n"
                                   + "\t [-h|-help|--help]             "
                                   + "\t Display this message.\n"
                                   + "\t [-n|-node] (default)          "
                                   + "\t Print drawables in TreeNodes.\n"
                                   + "\t [-t|-time]                    "
                                   + "\t Print drawables within endtimes.\n"
                                   + "\t [-ts time_start_factor]       "
                                   + "\t Default value is 0.0 (min).\n"
                                   + "\t [-tf time_final_factor]       "
                                   + "\t Default value is 1.0 (max).\n"
                                   + "\t [-d lowest_depth]             "
                                   + "\t Default value is 0 (leaf level).\n";

    private static void parseCmdLineArgs( String argv[] )
    {
        String        arg_str;
        StringBuffer  err_msg = new StringBuffer();
        int idx = 0;
        try {
            while ( idx < argv.length ) {
                if ( argv[ idx ].startsWith( "-" ) ) {
                    if (  argv[ idx ].equals( "-h" )
                       || argv[ idx ].equals( "-help" )
                       || argv[ idx ].equals( "--help" ) ) {
                        System.out.println( help_msg );
                        System.out.flush();
                        System.exit( 0 );
                    }
                    else if (  argv[ idx ].equals( "-n" )
                            || argv[ idx ].equals( "-node" ) ) {
                         printNode = true;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-t" )
                            || argv[ idx ].equals( "-time" ) ) {
                         printNode = false;
                         idx++;
                    }
                    else if ( argv[ idx ].equals( "-ts" ) ) {
                        arg_str = argv[ ++idx ];
                        time_init_ftr = Double.parseDouble( arg_str );
                        err_msg.append( "\n time_start_factor = " + arg_str );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-tf" ) ) {
                        arg_str = argv[ ++idx ];
                        time_final_ftr = Double.parseDouble( arg_str );
                        err_msg.append( "\n time_final_factor = " + arg_str );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-d" ) ) {
                        arg_str = argv[ ++idx ];
                        lowest_depth   = Short.parseShort( arg_str );
                        err_msg.append( "\n lowest_depth = " + arg_str );
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
        } catch ( NumberFormatException numerr ) {
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

        if (  time_init_ftr > time_final_ftr 
           || time_init_ftr < 0.0 || time_final_ftr > 1.0 ) {
            System.err.println( "Invalid time_init_factor "
                              + "and time_final_factor!" );
            System.err.println( "time_init_factor = " + time_init_ftr );
            System.err.println( "time_final_factor = " + time_final_ftr );
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

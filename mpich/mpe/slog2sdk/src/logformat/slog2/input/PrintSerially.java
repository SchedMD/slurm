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

import java.util.Iterator;


/*
   itrTopoLevel defines what topology should be printed.
   The possible values are defined in logformat.slog2.input.InputLog
*/
public class PrintSerially
{
    private static String         in_filename;
    private static Drawable.Order dobj_order   = Drawable.INCRE_STARTTIME_ORDER;
    private static int            itrTopoLevel = InputLog.ITERATE_ALL;
    private static double         time_init_ftr  = 0.0;
    private static double         time_final_ftr = 1.0;
    private static boolean        printCategoryMap  = true;
    private static boolean        printTreeDir      = true;
    private static boolean        printLineIDMaps   = true;
    private static boolean        printDrawables    = true;


    public static final void main( String[] args )
    {
        InputLog         slog_ins;
        CategoryMap      objdefs;
        TreeDir          treedir;
        TimeBoundingBox  timeframe;
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
        System.out.println( slog_ins.toString( printCategoryMap,
                                               printTreeDir,
                                               printLineIDMaps ) );
        if ( !printDrawables ) {
            slog_ins.close();
            System.exit( 0 );
        }

        treedir = slog_ins.getTreeDir();
        // System.out.println( treedir );

        TreeDirValue  root_dir;
        root_dir  = (TreeDirValue) treedir.get( treedir.firstKey() );
        timeframe = new TimeBoundingBox( root_dir.getTimeBoundingBox() );
        scaleTimeBounds( timeframe );

        boolean   isStartTimeOrdered;
        boolean   isIncreTimeOrdered;
        double    prev_bordertime;

        Iterator  dobj_itr;
        Drawable  dobj;
        double    dobj_bordertime;
        int       dobj_count;

        isStartTimeOrdered = dobj_order.isStartTimeOrdered();
        isIncreTimeOrdered = dobj_order.isIncreasingTimeOrdered();
        prev_bordertime    = isIncreTimeOrdered ?
                             Double.NEGATIVE_INFINITY :
                             Double.POSITIVE_INFINITY;
        dobj_count         = 0;

        dobj_itr = slog_ins.iteratorOfRealDrawables( timeframe, dobj_order,
                                                     itrTopoLevel );
        while ( dobj_itr.hasNext() ) {
            dobj            = (Drawable) dobj_itr.next();
            dobj_bordertime = dobj.getBorderTime( isStartTimeOrdered );
            if ( isIncreTimeOrdered ) {
                if ( prev_bordertime > dobj_bordertime )
                    System.out.print( "  *****  " );
            }
            else {
                if ( prev_bordertime < dobj_bordertime )
                    System.out.print( "  *****  " );
            }


            System.out.println( (++dobj_count) + ": " + dobj );
            // System.out.println( dobj + " <=> " + (++dobj_count) );
            // System.out.println( dobj  );
            // printClogArrowMessageSize( dobj );
            prev_bordertime  = dobj_bordertime;
        }

        slog_ins.close();
    }

    private static void printClogArrowMessageSize( Drawable dobj )
    {
        // Test code to extract InfoValue in Drawable
        if ( dobj.getCategory().getTopology().isArrow() ) {
            int infovals_length  = dobj.getInfoLength();
            if ( infovals_length >= 2 ) {
                Integer msg_sz = (Integer) dobj.getInfoValue( 1 ).getValue();
                System.out.println( dobj.getInfoKey( 1 ) 
                                  + msg_sz.intValue() );
            }
        }
    }

    private static void scaleTimeBounds( TimeBoundingBox endtimes )
    {
        double time_init  = endtimes.getEarliestTime();
        double time_final = endtimes.getLatestTime();
        double time_range = time_final - time_init;
        endtimes.setEarliestTime( time_init + time_init_ftr * time_range );
        endtimes.setLatestTime( time_init + time_final_ftr * time_range );
    }

    private static String help_msg = "Usage: java slog2.input.PrintSerially "
                                   + "[options] slog2_filename.\n"
                                   + "Options: \n"
                                   + "\t [-h|-help|--help]           "
                                   + "\t Display this message.\n"
                                   + "\t [-c|-category] DEF          "
                                   + "\t Print category map only.\n"
                                   + "\t [-d|-directory] DEF         "
                                   + "\t Print directory tree only.\n"
                                   + "\t [-y|-ycoordmap] DEF         "
                                   + "\t Print Y-coord. map only.\n"
                                   + "\t [-s|-state] DEF             "
                                   + "\t Print states only.\n"
                                   + "\t [-a|-arrow] DEF             "
                                   + "\t Print arrows only.\n"
                                   + "\t [-is|-incre_starttime] DEF  "
                                   + "\t Print in increasing starttime order.\n"
                                   + "\t [-ds|-decre_starttime]      "
                                   + "\t Print in decreasing starttime order.\n"
                                   + "\t [-ie|-incre_endtime]        "
                                   + "\t Print in increasing endtime order.\n"
                                   + "\t [-de|-decre_endtime]        "
                                   + "\t Print in decreasing endtime order.\n"
                                   + "\t [-ts time_start_factor]     "
                                   + "\t Default value is 0.0 (min).\n"
                                   + "\t [-tf time_final_factor]     "
                                   + "\t Default value is 1.0 (max).\n"
                                   + "*** The options marked by DEF "
                                   + "are enabled by default.\n";

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
                    else if (  argv[ idx ].equals( "-c" )
                            || argv[ idx ].equals( "-category" ) ) {
                         printCategoryMap = true;
                         printTreeDir     = false;
                         printLineIDMaps  = false;
                         printDrawables   = false;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-d" )
                            || argv[ idx ].equals( "-directory" ) ) {
                         printCategoryMap = false;
                         printTreeDir     = true;
                         printLineIDMaps  = false;
                         printDrawables   = false;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-c" )
                            || argv[ idx ].equals( "-ycoordmap" ) ) {
                         printCategoryMap = false;
                         printTreeDir     = false;
                         printLineIDMaps  = true;
                         printDrawables   = false;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-is" )
                            || argv[ idx ].equals( "-incre_starttime" ) ) {
                         dobj_order       = Drawable.INCRE_STARTTIME_ORDER;
                         printDrawables   = true;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-ds" )
                            || argv[ idx ].equals( "-decre_starttime" ) ) {
                         dobj_order       = Drawable.DECRE_STARTTIME_ORDER;
                         printDrawables   = true;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-ie" )
                            || argv[ idx ].equals( "-incre_endtime" ) ) {
                         dobj_order       = Drawable.INCRE_FINALTIME_ORDER;
                         printDrawables   = true;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-de" )
                            || argv[ idx ].equals( "-decre_endtime" ) ) {
                         dobj_order       = Drawable.DECRE_FINALTIME_ORDER;
                         printDrawables   = true;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-s" )
                            || argv[ idx ].equals( "-state" ) ) {
                         itrTopoLevel     = InputLog.ITERATE_STATES;
                         printDrawables   = true;
                         idx++;
                    }
                    else if (  argv[ idx ].equals( "-a" )
                            || argv[ idx ].equals( "-arrow" ) ) {
                         itrTopoLevel     = InputLog.ITERATE_ARROWS;
                         printDrawables   = true;
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

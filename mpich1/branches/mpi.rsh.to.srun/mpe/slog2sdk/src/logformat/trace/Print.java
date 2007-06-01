/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.trace;

import java.util.*;
import base.drawable.*;

public class Print
{
    static {
        System.loadLibrary( "TraceInput" );
    }

    private static String   in_filename, out_filename;
    private static boolean  enable_endtime_check;

    public static final void main( String[] args )
    {
        logformat.trace.InputLog    dobj_ins;
        Kind                        next_kind;
        Topology                    topo;
        Map                         objdefs;   // Drawable def'n
        Map                         shadefs;   // Shadow   def'n
        Category                    objdef;
        YCoordMap                   ymap;
        Primitive                   prime_obj;
        Composite                   cmplx_obj;
        long                        Nobjs;

        double                      prev_dobj_endtime;
        double                      curr_dobj_endtime;
        long                        offended_Nobjs;
        Drawable                    offended_dobj;

        //  Initialize prev_dobj_endtime to avoid complaint by compiler
        prev_dobj_endtime = Double.NEGATIVE_INFINITY;
        offended_Nobjs    = Integer.MIN_VALUE;
        offended_dobj     = null;
        parseCmdLineArgs( args );

        /*
           objdefs can be either slog2.CategoryMap or a Map 
           for this simple Print
           objdefs       = new CategoryMap();
        */
        objdefs       = new HashMap();
        shadefs       = new HashMap();
        Nobjs         = 0;

        /* */    Date time1 = new Date();
        dobj_ins   = new logformat.trace.InputLog( in_filename );

        /* */    Date time2 = new Date();
        while ( ( next_kind = dobj_ins.peekNextKind() ) != Kind.EOF ) {
            if ( next_kind == Kind.TOPOLOGY ) {
                topo = dobj_ins.getNextTopology();
                objdef = Category.getShadowCategory( topo );
                objdefs.put( new Integer( objdef.getIndex() ), objdef );
                shadefs.put( topo, objdef );
                System.out.println( "trace.Print: " + topo );
                System.out.println( "trace.Print: " + objdef );
            }
            else if ( next_kind == Kind.YCOORDMAP ) {
                ymap = dobj_ins.getNextYCoordMap();
                System.out.println( "trace.Print: " + ymap );
            }
            else if ( next_kind == Kind.CATEGORY ) {
                objdef = dobj_ins.getNextCategory();
                objdefs.put( new Integer( objdef.getIndex() ), objdef );
                System.out.println( "trace.Print: " + objdef );
            } 
            else if ( next_kind == Kind.PRIMITIVE ) {
                prime_obj = dobj_ins.getNextPrimitive();
                prime_obj.resolveCategory( objdefs );
                // postponed, wait till on-demand decoding of InfoBuffer
                // prime_obj.setInfoValues();
                Nobjs++;
                System.out.println( Nobjs + " : " + prime_obj );
                if ( enable_endtime_check ) {
                    if ( ! prime_obj.isTimeOrdered() ) {
                        System.out.println( "**** Primitive Time Error ****" );
                        System.exit( 1 );
                    }
                    curr_dobj_endtime = prime_obj.getLatestTime();
                    if ( prev_dobj_endtime > curr_dobj_endtime ) {
                        System.out.println( "**** Violation of "
                                          + "Increasing Endtime Order ****\n"
                                          + "\t Offended Drawable -> "
                                          + offended_Nobjs + " : "
                                          + offended_dobj + "\n"
                                          + "\t Offending Primitive -> "
                                          + Nobjs + " : " + prime_obj + "\n" 
                                          + "   previous drawable endtime ( "
                                          + prev_dobj_endtime + " ) "
                                          + " > current drawable endtiime ( "
                                          + curr_dobj_endtime + " ) " );
                        System.exit( 1 );
                    }
                    else {
                        offended_Nobjs    = Nobjs;
                        offended_dobj     = prime_obj;
                        prev_dobj_endtime = curr_dobj_endtime;
                    }
                }
            }
            else if ( next_kind == Kind.COMPOSITE ) {
                cmplx_obj = dobj_ins.getNextComposite();
                cmplx_obj.resolveCategory( objdefs );
                // postponed, wait till on-demand decoding of InfoBuffer
                // cmplx_obj.setInfoValues();
                Nobjs++;
                System.out.println( Nobjs + " : " + cmplx_obj );
                if ( enable_endtime_check ) {
                    if ( ! cmplx_obj.isTimeOrdered() ) {
                        System.out.println( "**** Composite Time Error ****" );
                        System.exit( 1 );
                    }
                    curr_dobj_endtime = cmplx_obj.getLatestTime();
                    if ( prev_dobj_endtime > curr_dobj_endtime ) {
                        System.out.println( "**** Violation of "
                                          + "Increasing Endtime Order ****\n"
                                          + "\t Offended Drawable -> " 
                                          + offended_Nobjs + " : "
                                          + offended_dobj + "\n"
                                          + "\t Offending Composite -> "
                                          + Nobjs + " : " + cmplx_obj + "\n" 
                                          + "   previous drawable endtime ( "
                                          + prev_dobj_endtime + " ) "
                                          + " > current drawable endtiime ( "
                                          + curr_dobj_endtime + " ) " );
                        System.exit( 1 );
                    }
                    else {
                        offended_Nobjs    = Nobjs;
                        offended_dobj     = cmplx_obj;
                        prev_dobj_endtime = curr_dobj_endtime;
                    }
                }
            }
            else {
                System.err.println( "trace.Print: Unrecognized return "
                                  + "from peekNextKind() = " + next_kind );
            }
        }   // Endof while ( dobj_ins.peekNextKind() )

        dobj_ins.close();

        /* */    Date time3 = new Date();
        System.err.println( "\n" );
        System.err.println( "Number of Drawables = " + Nobjs );

        // System.err.println( "time1 = " + time1 + ", " + time1.getTime() );
        // System.err.println( "time2 = " + time2 + ", " + time2.getTime() );
        // System.err.println( "time3 = " + time3 + ", " + time3.getTime() );
        System.err.println( "timeElapsed between 1 & 2 = "
                          + ( time2.getTime() - time1.getTime() ) + " msec" );
        System.err.println( "timeElapsed between 2 & 3 = "
                          + ( time3.getTime() - time2.getTime() ) + " msec" );
    }


    private static String help_msg = "Usage: java trace.Print "
                                   + "[options] trace_filename.\n"
                                   + " options: \n"
                                   + "\t [-h|--h|-help|--help]            "
                                   + "\t Display this message.\n"
                                   + "\t [-tc]                            "
                                   + "\t Check increasing endtime order\n";

    private static void parseCmdLineArgs( String argv[] )
    {
        String        arg_str;
        int           idx;
        StringBuffer  err_msg       = new StringBuffer();
        StringBuffer  filespec_buf  = new StringBuffer();
        idx = 0;
        enable_endtime_check = false;
        try {
            while ( idx < argv.length ) {
                if ( argv[ idx ].startsWith( "-" ) ) {
                    if (  argv[ idx ].equals( "-h" ) 
                       || argv[ idx ].equals( "--h" )
                       || argv[ idx ].equals( "-help" )
                       || argv[ idx ].equals( "--help" ) ) {
                        System.out.println( help_msg );
                        filespec_buf.append( "-h " );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-tc" ) ) {
                        enable_endtime_check = true;
                        err_msg.append( "\n endtime_order_check = true" );
                        idx++;
                    }
                    else {
                        filespec_buf.append( argv[ idx ] + " " );
                        idx++;
                    }
                }
                else {
                    filespec_buf.append( argv[ idx ] + " " );
                    idx++;
                }
            }
        } catch ( ArrayIndexOutOfBoundsException idxerr ) {
            if ( err_msg.length() > 0 )
                System.err.println( err_msg.toString() );
            System.err.println( "Error occurs after option "
                              + argv[ idx-1 ] + ", " + indexOrderStr( idx )
                              + " command line argument." );
            // System.err.println( help_msg );
            idxerr.printStackTrace();
        } catch ( NumberFormatException numerr ) {
            if ( err_msg.length() > 0 )
                System.err.println( err_msg.toString() );
            String idx_order_str = indexOrderStr( idx );
            System.err.println( "Error occurs after option "
                              + argv[ idx-1 ] + ", " + indexOrderStr( idx )
                              + " command line argument.  It needs a number." );
            // System.err.println( help_msg );
            numerr.printStackTrace();
        }
        
        in_filename = filespec_buf.toString().trim();
        if ( in_filename == null ) {
            System.err.println( "The Program needs a TRACE filename as "
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

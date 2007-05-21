/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.output;

import java.util.*;
import java.io.File;

import base.drawable.*;
import logformat.clog2TOdrawable.*;
import logformat.slog2.*;

public class Clog2ToSlog2
{
    private static short    num_children_per_node = 0;
    private static int      leaf_bytesize         = 0;
    private static String   in_filename, out_filename;
    private static boolean  enable_endtime_check;
    private static boolean  continue_when_violation;

    public static final void main( String[] args )
    {
        logformat.clog2TOdrawable.InputLog  dobj_ins;
        logformat.slog2.output.OutputLog    slog_outs;
        Kind                                next_kind;
        Topology                            topo;
        CategoryMap                         objdefs;   // Drawable def'n
        Map                                 shadefs;   // Shadow   def'n
        Category                            objdef;
        LineIDMapList                       lineIDmaps;
        LineIDMap                           lineIDmap;
        Primitive                           prime_obj;
        Composite                           cmplx_obj;
        long                                Nobjs;

        double                              prev_dobj_endtime;
        double                              curr_dobj_endtime;
        TreeTrunk                           treetrunk;



        //  Initialize prev_dobj_endtime to avoid complaint by compiler
        prev_dobj_endtime = Double.NEGATIVE_INFINITY;

        out_filename = null;
        parseCmdLineArgs( args );
        if ( out_filename == null )
            out_filename  = TraceName.getDefaultSLOG2Name( in_filename );

        objdefs       = new CategoryMap();
        shadefs       = new HashMap();
        lineIDmaps    = new LineIDMapList();
        Nobjs         = 0;

        /* */    Date time1 = new Date();
        dobj_ins   = new logformat.clog2TOdrawable.InputLog( in_filename );
        slog_outs  = new logformat.slog2.output.OutputLog( out_filename );

        //  Set Tree properties, !optional!
        //  TreeNode's minimum size, without any drawable/shadow, is 38 bytes.
        //  Drawable;s minimum size is 32 bytes, whether it is state/arrow.
        //  Arrow( with 2 integer infovalues ) is 40 bytes long.
        //  So, for 1 state primitive leaf, the size is 38 + 40 = 78 .
        if ( leaf_bytesize > 0 )
            slog_outs.setTreeLeafByteSize( leaf_bytesize );
        if ( num_children_per_node > 0 )
            slog_outs.setNumChildrenPerNode( num_children_per_node );

        treetrunk = new TreeTrunk( slog_outs, shadefs );
        /* */    Date time2 = new Date();
        while ( ( next_kind = dobj_ins.peekNextKind() ) != Kind.EOF ) {
            if ( next_kind == Kind.TOPOLOGY ) {
                topo = dobj_ins.getNextTopology();
                objdef = Category.getShadowCategory( topo );
                objdefs.put( new Integer( objdef.getIndex() ), objdef );
                shadefs.put( topo, objdef );
                // System.out.println( "TOPOLOGY: " + topo );
            }
            else if ( next_kind == Kind.YCOORDMAP ) {
                lineIDmap = new LineIDMap( dobj_ins.getNextYCoordMap() );
                lineIDmaps.add( lineIDmap );
                // System.out.println( "YCOORDMAP: = " + lineIDmap );
            }
            else if ( next_kind == Kind.CATEGORY ) {
                objdef = dobj_ins.getNextCategory();
                objdefs.put( new Integer( objdef.getIndex() ), objdef );
                // System.out.println( "CATEGORY: " + objdef );
            } 
            else if ( next_kind == Kind.PRIMITIVE ) {
                prime_obj = dobj_ins.getNextPrimitive();
                // prime_obj's Category has been set in clog2TOdrawable.InputLog
                // here, resolveCategory() set Category.setUsed( TRUE )
                prime_obj.resolveCategory( objdefs );
                Nobjs++;
                // System.out.println( Nobjs
                //                   + ", bytesize=" + prime_obj.getByteSize()
                //                   + " : " + prime_obj );
                if ( enable_endtime_check ) {
                    if ( ! prime_obj.isTimeOrdered() ) {
                        System.out.println( "**** Primitive Time Error ****" );
                        if ( ! continue_when_violation )
                            System.exit( 1 );
                    }
                    curr_dobj_endtime = prime_obj.getLatestTime();
                    if ( prev_dobj_endtime > curr_dobj_endtime ) {
                        System.err.println( "***** Violation of "
                                          + "Increasing Endtime Order! *****\n"
                                          + "   previous drawable endtime ( "
                                          + prev_dobj_endtime + " ) "
                                          + " > current drawable endtiime ( "
                                          + curr_dobj_endtime + " ) " );
                        if ( ! continue_when_violation )
                            System.exit( 1 );
                    }
                    prev_dobj_endtime = curr_dobj_endtime;
                }
                treetrunk.addDrawable( prime_obj );
            }
            else {
                System.err.println( "ClogToSlog2: Unrecognized return "
                                  + "from peekNextKind() = " + next_kind );
            }
        }   // Endof while ( dobj_ins.peekNextKind() )

        // Check if flushToFile is successful,
        // i.e. if treetrunk contains drawables.
        if ( treetrunk.flushToFile() ) {
            objdefs.removeUnusedCategories();
            slog_outs.writeCategoryMap( objdefs );

            lineIDmaps.add( treetrunk.getIdentityLineIDMap() );
            slog_outs.writeLineIDMapList( lineIDmaps );

            slog_outs.close();
            dobj_ins.close();
        }
        else {
            slog_outs.close();
            dobj_ins.close();
            System.err.println( "Error: No drawable is found in the trace "
                              + in_filename );
            File slog_file  = new File( out_filename );
            if ( slog_file.isFile() && slog_file.exists() ) {
                System.err.println( "       Removing the remnants of the file "
                                  + out_filename + " ....." );
                slog_file.delete();
            }
            System.exit( 1 );
        }

        /* */    Date time3 = new Date();
        System.out.println( "\n" );
        System.out.println( "Number of Drawables = " + Nobjs );
        System.out.println( "Number of Unmatched Events = "
                          + dobj_ins.getNumberOfUnMatchedEvents() );

        System.out.println( "Total ByteSize of the logfile = "
                          + dobj_ins.getTotalBytesRead() );
        // System.out.println( "time1 = " + time1 + ", " + time1.getTime() );
        // System.out.println( "time2 = " + time2 + ", " + time2.getTime() );
        // System.out.println( "time3 = " + time3 + ", " + time3.getTime() );
        System.out.println( "timeElapsed between 1 & 2 = "
                          + ( time2.getTime() - time1.getTime() ) + " msec" );
        System.out.println( "timeElapsed between 2 & 3 = "
                          + ( time3.getTime() - time2.getTime() ) + " msec" );
    }

    private static int parseByteSize( String size_str )
    {
        int idxOfKilo = Math.max( size_str.indexOf( 'k' ),
                                  size_str.indexOf( 'K' ) );
        int idxOfMega = Math.max( size_str.indexOf( 'm' ),
                                  size_str.indexOf( 'M' ) );
        if ( idxOfKilo > 0 )
            return Integer.parseInt( size_str.substring( 0, idxOfKilo ) )
                   * 1024;
        else if ( idxOfMega > 0 )
            return Integer.parseInt( size_str.substring( 0, idxOfMega ) )
                   * 1024 * 1024;
        else
            return Integer.parseInt( size_str );
    }

    private static String help_msg = "Usage: java slog2.output.Clog2ToSlog2 "
                                   + "[options] clog2_filename.\n"
                                   + " options: \n"
                                   + "\t [-h|--h|-help|--help]             "
                                   + "\t Display this message.\n"
                                   + "\t [-tc]                             "
                                   + "\t Check increasing endtime order,\n"
                                   + "\t                                   "
                                   + "\t exit when 1st violation occurs.\n"
                                   + "\t [-tcc]                            "
                                   + "\t Check increasing endtime order,\n"
                                   + "\t                                   "
                                   + "\t continue when violations occur.\n"
                                   + "\t [-nc number_of_children_per_node] "
                                   + "\t Default value is "
                                   + logformat.slog2.Const.NUM_LEAFS +".\n"
                                   + "\t [-ls max_byte_size_of_leaf_node]  "
                                   + "\t Default value is "
                                   + logformat.slog2.Const.LEAF_BYTESIZE +".\n"
                                   + "\t [-o output_filename_with_slog2_suffix]"
                                   + "\n\n"
                                   + " note: \"max_byte_size_of_leaf_node\" "
                                   + "can be specified with suffix "
                                   + "k, K, m or M,\n"
                                   + "       where k or K stands for kilobyte,"
                                   + " m or M stands for megabyte.\n"
                                   + "       e.g. 64k means 65536 bytes.\n";

    private static void parseCmdLineArgs( String argv[] )
    {
        String        arg_str;
        int           idx;
        StringBuffer  err_msg = new StringBuffer();

        in_filename              = null;
        enable_endtime_check     = false;
        continue_when_violation  = false;

        if ( argv.length == 0 ) {
            System.out.println( help_msg );
            System.out.flush();
            System.exit( 0 );
        }

        idx = 0;
        try {
            while ( idx < argv.length ) {
                if ( argv[ idx ].startsWith( "-" ) ) {
                    if (  argv[ idx ].equals( "-h" ) 
                       || argv[ idx ].equals( "--h" )
                       || argv[ idx ].equals( "-help" )
                       || argv[ idx ].equals( "--help" ) ) {
                        System.out.println( help_msg );
                        System.out.flush();
                        System.exit( 0 );
                    }
                    else if ( argv[ idx ].equals( "-tc" ) ) {
                        enable_endtime_check     = true;
                        continue_when_violation  = false;
                        err_msg.append( "\n endtime_order_check_exit = true" );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-tcc" ) ) {
                        enable_endtime_check     = true;
                        continue_when_violation  = true;
                        err_msg.append( "\n endtime_order_check_stay = true" );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-nc" ) ) {
                        arg_str = argv[ ++idx ]; 
                        num_children_per_node = Short.parseShort( arg_str );
                        err_msg.append( "\n number_of_children_per_node = "
                                      + arg_str );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-ls" ) ) {
                        arg_str = argv[ ++idx ];
                        leaf_bytesize = parseByteSize( arg_str );
                        err_msg.append( "\n max_byte_size_of_leaf_node = "
                                      + arg_str );
                        idx++;
                    }
                    else if ( argv[ idx ].equals( "-o" ) ) {
                        out_filename = argv[ ++idx ].trim();
                        err_msg.append( "\n output_filename = "
                                      + out_filename );
                        idx++;
                        if ( ! out_filename.endsWith( ".slog2" ) )
                            System.err.println( "Warning: The suffix of the "
                                              + "output filename is NOT "
                                              + "\".slog2\"." );
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
        
        if ( in_filename == null ) {
            System.err.println( "The Program needs a CLOG2 filename as "
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

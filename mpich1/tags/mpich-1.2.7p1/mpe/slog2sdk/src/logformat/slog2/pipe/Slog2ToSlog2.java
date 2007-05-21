/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.pipe;

import base.drawable.Kind;
import base.drawable.Topology;
import base.drawable.Category;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Composite;
import base.drawable.YCoordMap;
import logformat.slog2.LineIDMap;
import logformat.slog2.CategoryMap;
import logformat.slog2.LineIDMapList;
import logformat.slog2.TraceName;
import logformat.slog2.output.TreeTrunk;
import logformat.slog2.output.OutputLog;

import java.util.Date;
import java.util.Arrays;
import java.util.Map;
import java.util.HashMap;
import java.util.StringTokenizer;

public class Slog2ToSlog2
{
    private static short    num_children_per_node = 0;
    private static int      leaf_bytesize         = 0;
    private static String   in_filename, out_filename;
    private static boolean  enable_endtime_check;
    private static boolean  continue_when_violation;
    // Category indexes to be deleted
    private static int[]    del_iobjdefs;

    public static final void main( String[] args )
    {
        PipedInputLog      dobj_ins;
        OutputLog          slog_outs;
        Kind               next_kind;
        Topology           topo;
        CategoryMap        objdefs;   // Drawable def'n
        Map                shadefs;   // Shadow   def'n
        Category           objdef;
        LineIDMapList      lineIDmaps;
        LineIDMap          lineIDmap;
        Primitive          prime_obj;
        Composite          cmplx_obj;
        long               Nobjs;

        TreeTrunk          treetrunk;
        double             prev_dobj_endtime;
        double             curr_dobj_endtime;
        long               offended_Nobjs;
        Drawable           offended_dobj;



        //  Initialize prev_dobj_endtime to avoid complaint by compiler
        prev_dobj_endtime = Double.NEGATIVE_INFINITY;
        offended_Nobjs    = Integer.MIN_VALUE;
        offended_dobj     = null;

        out_filename      = null;
        del_iobjdefs      = null;
        parseCmdLineArgs( args );
        if ( out_filename == null )
            out_filename  = TraceName.getDefaultSLOG2Name( in_filename );

        objdefs       = new CategoryMap();
        shadefs       = new HashMap();
        lineIDmaps    = new LineIDMapList();
        Nobjs         = 0;

        // Initialize the SLOG-2 file for piped-input, output for this program.
        dobj_ins   = new PipedInputLog( in_filename );
        if ( dobj_ins == null ) {
            System.err.println( "Null input logfile!" );
            System.exit( 1 );
        }
        if ( ! dobj_ins.isSLOG2() ) {
            System.err.println( in_filename + " is NOT SLOG-2 file!." );
            System.exit( 1 );
        }
        String err_msg;
        if ( (err_msg = dobj_ins.getCompatibleHeader()) != null ) {
            System.err.print( err_msg );
            PipedInputLog.stdoutConfirmation();
        }
        dobj_ins.initialize();

        /* */    Date time1 = new Date();
        slog_outs  = new OutputLog( out_filename );

        //  Set Tree properties, !optional!
        //  TreeNode's minimum size, without any drawable/shadow, is 38 bytes.
        //  Drawable;s minimum size is 32 bytes, whether it is state/arrow.
        //  Arrow( with 2 integer infovalues ) is 40 bytes long.
        //  So, for 1 state primitive leaf, the size is 38 + 40 = 78 .
        if ( leaf_bytesize > 0 )
            slog_outs.setTreeLeafByteSize( leaf_bytesize );
        else
            slog_outs.setTreeLeafByteSize( dobj_ins.getTreeLeafByteSize() );
        if ( num_children_per_node > 0 )
            slog_outs.setNumChildrenPerNode( num_children_per_node );
        else
            slog_outs.setNumChildrenPerNode( dobj_ins.getNumChildrenPerNode() );

        treetrunk = new TreeTrunk( slog_outs, shadefs );
        /* */    Date time2 = new Date();
        while ( ( next_kind = dobj_ins.peekNextKind() ) != Kind.EOF ) {
            if ( next_kind == Kind.TOPOLOGY ) {
                topo = dobj_ins.getNextTopology();
                // Put in the default Shadow categories in case the original
                // does not have any shadow categories, i.e no shadows.
                objdef = Category.getShadowCategory( topo );
                objdefs.put( new Integer( objdef.getIndex() ), objdef );
                shadefs.put( topo, objdef );
            }
            else if ( next_kind == Kind.YCOORDMAP ) {
                lineIDmap = new LineIDMap( dobj_ins.getNextYCoordMap() );
                lineIDmaps.add( lineIDmap );
            }
            else if ( next_kind == Kind.CATEGORY ) {
                objdef = dobj_ins.getNextCategory();
                if ( objdef.isShadowCategory() ) {
                    objdefs.put( new Integer( objdef.getIndex() ), objdef );
                    shadefs.put( objdef.getTopology(), objdef );
                }
                // Category can be removed here for efficiency reason.
                // Instead let CategoryMap.removeUnusedCategories() do the work.
                // if ( isCategoryToBeRemoved( objdef.getIndex() ) )
                //     continue;
                objdefs.put( new Integer( objdef.getIndex() ), objdef );
                objdef.setUsed( false );
            } 
            else if ( next_kind == Kind.PRIMITIVE ) {
                prime_obj = dobj_ins.getNextPrimitive();
                // Undo InfoBox.resolveCategory() when the Drawable is read.
                prime_obj.releaseCategory();
                if ( isCategoryToBeRemoved( prime_obj.getCategoryIndex() ) ) {
                    // System.out.println( "Removing ... " + prime_obj );
                    continue;
                }
                prime_obj.resolveCategory( objdefs );
                // postponed, wait till on-demand decoding of InfoBuffer
                // prime_obj.decodeInfoBuffer();
                Nobjs++;
                // System.out.println( Nobjs + " : " + prime_obj );
                if ( enable_endtime_check ) {
                    if ( ! prime_obj.isTimeOrdered() ) {
                        System.out.println( "**** Primitive Time Error ****" );
                        if ( ! continue_when_violation )
                            System.exit( 1 );
                    }
                    curr_dobj_endtime = prime_obj.getLatestTime();
                    if ( prev_dobj_endtime > curr_dobj_endtime ) {
                        System.err.println( "**** Violation of "
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
                        if ( ! continue_when_violation )
                            System.exit( 1 );
                    }
                    offended_Nobjs    = Nobjs;
                    offended_dobj     = prime_obj;
                    prev_dobj_endtime = curr_dobj_endtime;
                }
                treetrunk.addDrawable( prime_obj );
            }
            else if ( next_kind == Kind.COMPOSITE ) {
                cmplx_obj = dobj_ins.getNextComposite();
                // Undo InfoBox.resolveCategory() when the Drawable is read.
                cmplx_obj.releaseCategory();
                if ( isCategoryToBeRemoved( cmplx_obj.getCategoryIndex() ) ) {
                    // System.out.println( "Removing ... " + cmplx_obj );
                    continue;
                }
                cmplx_obj.resolveCategory( objdefs );
                // postponed, wait till on-demand decoding of InfoBuffer
                // cmplx_obj.decodeInfoBuffer();
                Nobjs++;
                // System.out.println( Nobjs + " : " + cmplx_obj );
                if ( enable_endtime_check ) {
                    if ( ! cmplx_obj.isTimeOrdered() ) {
                        System.out.println( "**** Composite Time Error ****" );
                        if ( ! continue_when_violation )
                            System.exit( 1 );
                    }
                    curr_dobj_endtime = cmplx_obj.getLatestTime();
                    if ( prev_dobj_endtime > curr_dobj_endtime ) {
                        System.err.println( "***** Violation of "
                                          + "Increasing Endtime Order! *****\n"
                                          + "\t Offended Drawable -> "
                                          + offended_Nobjs + " : "
                                          + offended_dobj + "\n"
                                          + "\t Offending Composite -> "
                                          + Nobjs + " : " + cmplx_obj + "\n"
                                          + "   previous drawable endtime ( "
                                          + prev_dobj_endtime + " ) "
                                          + " > current drawable endtiime ( "
                                          + curr_dobj_endtime + " ) " );
                        if ( ! continue_when_violation )
                            System.exit( 1 );
                    }
                    offended_Nobjs    = Nobjs;
                    offended_dobj     = cmplx_obj;
                    prev_dobj_endtime = curr_dobj_endtime;
                }
                treetrunk.addDrawable( cmplx_obj );
            }
            else {
                System.err.println( "Slog2ToSlog2: Unrecognized return "
                                  + "from peekNextKind() = " + next_kind );
            }
        }   // Endof while ( dobj_ins.peekNextKind() )
        treetrunk.flushToFile();

        objdefs.removeUnusedCategories();
        slog_outs.writeCategoryMap( objdefs );

        // treetrunk's IdentityLineIDMap could be duplicate of the one
        // in the logfile
        // lineIDmaps.add( treetrunk.getIdentityLineIDMap() );
        slog_outs.writeLineIDMapList( lineIDmaps );

        slog_outs.close();
        dobj_ins.close();

        /* */    Date time3 = new Date();
        System.out.println( "\n" );
        System.out.println( "Number of Drawables = " + Nobjs );

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

    private static String help_msg = "Usage: java slog2.output.Slog2ToSlog2 "
                                   + "[options] slog2_filename.\n"
                                   + " options: \n"
                                   + "\t [-h|--h|-help|--help]             "
                                   + "\t Display HELP message.\n"
                                   + "\t [-tc]                             "
                                   + "\t Check increasing endtime order,\n"
                                   + "\t                                   "
                                   + "\t exit when 1st violation occurs.\n"
                                   + "\t [-tcc]                            "
                                   + "\t Check increasing endtime order,\n"
                                   + "\t                                   "
                                   + "\t continue when violations occur.\n"
                                   + "\t [-nc number_of_children_per_node] "
                                   + "\t Default value is taken from the\n"
                                   + "\t                                   "
                                   + "\t input slog2 file.\n"
                                   + "\t [-ls max_byte_size_of_leaf_node]  "
                                   + "\t Default value is taken from the\n"
                                   + "\t                                   "
                                   + "\t input slog2 file.\n"
                                   + "\t [-r id1,id2,...,idN]              "
                                   + "\t Remove drawable categories of  \n"
                                   + "\t                                   "
                                   + "\t indexes, id1,id2,...,idN.\n"
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
        StringBuffer  err_msg    = new StringBuffer();

        in_filename              = null;
        enable_endtime_check     = false;
        continue_when_violation  = false;
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
                    else if ( argv[ idx ].equals( "-r" ) ) {
                        arg_str = argv[ ++idx ];
                        parseRemovalCategoryIndexes( arg_str );
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
            System.err.println( "This program needs a SLOG2 filename "
                              + "as part of the command line arguments." );
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

    private static void parseRemovalCategoryIndexes( String arg_str )
    {
        StringTokenizer idxs_itr;
        int             ii;

        idxs_itr = new StringTokenizer( arg_str.trim(), "," );
        del_iobjdefs = new int[ idxs_itr.countTokens() ];
        for ( ii = 0; idxs_itr.hasMoreTokens(); ii++ )
            del_iobjdefs[ ii ] = Integer.parseInt( idxs_itr.nextToken() );
        // Sort del_iobjdefs[] into ascending order
        Arrays.sort( del_iobjdefs );
    }

    private static boolean isCategoryToBeRemoved( int iobjdef )
    {
        if ( del_iobjdefs != null ) {
            if ( Arrays.binarySearch( del_iobjdefs, iobjdef ) >= 0 )
                return true;
        }
        return false;
    }
}

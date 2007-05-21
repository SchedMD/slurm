/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.trace;

import base.drawable.*;

/*
   This class provides the Java version of TRACE-API.
*/
public class InputLog // implements drawable.InputAPI
{
    private String    filespec;
    private long      filehandle;

    private int       num_topology_returned;

    public InputLog( String spec_str )
    {
        boolean isOK;
        filespec = spec_str;
        isOK = this.open();        // set filehandle
        if ( filehandle == 0 ) {
            if ( isOK ) {
                System.out.println( "trace.InputLog.open() exits normally!" );
                System.exit( 0 );
            }
            else {
                System.err.println( "trace.InputLog.open() fails!\n"
                      + "No slog2 file is generated due to previous errors." );
                System.exit( 1 );
            }
        }

        // Initialize Topology name return counter
        num_topology_returned = 0;
    }

    private native static void initIDs();

    public  native boolean    open();

    public  native boolean    close();

    public  native int        peekNextKindIndex();

    public  native Category   getNextCategory();

    public  native YCoordMap  getNextYCoordMap();

    public  native Primitive  getNextPrimitive();

    public  native Composite  getNextComposite();

    static {
        initIDs();
    }

    public Kind  peekNextKind()
    {
        // Return all the Topology names.
        if ( num_topology_returned < 3 )
            return Kind.TOPOLOGY;

        int next_kind_index  = this.peekNextKindIndex();
        switch ( next_kind_index ) {
            case Kind.TOPOLOGY_ID :
                return Kind.TOPOLOGY;
            case Kind.EOF_ID :
                return Kind.EOF;
            case Kind.PRIMITIVE_ID :
                return Kind.PRIMITIVE;
            case Kind.COMPOSITE_ID :
                return Kind.COMPOSITE;
            case Kind.CATEGORY_ID :
                return Kind.CATEGORY;
            case Kind.YCOORDMAP_ID :
                return Kind.YCOORDMAP;
            default :
                System.err.println( "trace.InputLog.peekNextKind(): "
                                  + "Unknown value, " + next_kind_index );
        }
        return null;
    }

    public Topology getNextTopology()
    {
        switch ( num_topology_returned ) {
            case 0:
                num_topology_returned = 1;
                return Topology.EVENT;
            case 1:
                num_topology_returned = 2;
                return Topology.STATE;
            case 2:
                num_topology_returned = 3;
                return Topology.ARROW;
            default:
                System.err.println( "All Topology Names have been returned" );
        }
        return null;
    }
}

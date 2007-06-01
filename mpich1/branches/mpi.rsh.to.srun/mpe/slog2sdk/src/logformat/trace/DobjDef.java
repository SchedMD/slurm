/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.trace;

import java.util.StringTokenizer;

import base.drawable.Category;
import base.drawable.Topology;
import base.drawable.ColorAlpha;

public class DobjDef extends Category
{
    public DobjDef( int objidx, String objname, int shapeID,
                    int red, int green, int blue, int alpha, int width,
                    String labels, int[] methodIDs )
    {
        super( objidx, objname, width );
        super.setColor( new ColorAlpha( red, green, blue, alpha, true ) );

        Topology topo = new Topology( shapeID );
        if ( ! topo.isPrimitive() ) {
            String err_msg = "trace.DobjDef(): unknown shapeID = " + shapeID;
            throw new IllegalArgumentException( err_msg );
            // System.exit( 1 );
        }
        super.setTopology( topo );

        super.setInfoKeys( labels );
        super.setMethodIDs( methodIDs );
    }

    public static final void main( String[] args )
    {
        DobjDef objdef = new DobjDef( 100, "MPI_Init", 2,
                                      255, 255, 255, 10, 1,
                                      // "hello\nworld\n", new int[]{ 1 } );
                                      "hello\nworld\n", null );
        System.out.println( "DobjDef = " + objdef );
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.awt.Graphics2D;
import java.awt.Color;
import java.awt.Point;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
import base.topology.Line;
import base.topology.Arrow;
import base.topology.State;
import base.topology.Event;

// Primitive should be considered as an InfoBox of Coord[]

// Cloneable interface is for the creation of Shadow. 
public class Primitive extends Drawable
                       implements MixedDataIO
//                       implements Cloneable
{
    private static final int    INIT_BYTESIZE  = 2  /* vertices.length */ ; 

    private   Coord[]      vertices;
    private   int          last_vtx_idx;

    public Primitive()
    {
        super();
        vertices        = null;
    }

    public Primitive( int Nvertices )
    {
        super();
        vertices        = new Coord[ Nvertices ];
        last_vtx_idx    = vertices.length - 1;
    }

    public Primitive( Category in_type, int Nvertices )
    {
        super( in_type );
        vertices        = new Coord[ Nvertices ];
        last_vtx_idx    = vertices.length - 1;
    }

    //  This is NOT a copy constructor,
    //  only Category and InfoType[] are copied, not InfoValue[].
    public Primitive( final Primitive prime )
    {
        super( prime );
        Coord[] prime_vtxs = prime.vertices;
        vertices        = new Coord[ prime_vtxs.length ];
        for ( int idx = 0; idx < vertices.length; idx++ )
            vertices[ idx ] = new Coord( prime_vtxs[ idx ] );
        last_vtx_idx    = vertices.length - 1;
    }

    public Primitive( Category in_type, final Primitive prime )
    {
        super( in_type, prime );
        Coord[] prime_vtxs = prime.vertices;
        vertices        = new Coord[ prime_vtxs.length ];
        for ( int idx = 0; idx < vertices.length; idx++ )
            vertices[ idx ] = new Coord( prime_vtxs[ idx ] );
        last_vtx_idx    = vertices.length - 1;
    }

    // Class to support JNI in TRACE-API
    public Primitive( int type_idx, double starttime, double endtime,
                      double[] time_coords, int[] y_coords,
                      byte[]   byte_infovals )
    {
        super( type_idx, byte_infovals );
        super.setEarliestTime( starttime );
        super.setLatestTime( endtime );

        int time_coords_length = time_coords.length;
        vertices = new Coord[ time_coords_length ];
        if ( time_coords_length == y_coords.length ) {
            for ( int idx = 0; idx < time_coords_length; idx++ )
                vertices[ idx ] = new Coord( time_coords[ idx ],
                                             y_coords[ idx ] );
        }
        else {
            for ( int idx = 0; idx < time_coords_length; idx++ )
                vertices[ idx ] = new Coord( time_coords[ idx ],
                                             y_coords[ idx / 2 ] );
        }
        // super.affectTimeBounds( vertices );
    }

    /*  Abstract method of Drawable  */
    public int getNumOfPrimitives()
    {
        return 1;
    }

    public int getByteSize()
    {
        int bytesize;
        bytesize = super.getByteSize() + INIT_BYTESIZE;
        if ( vertices != null && vertices.length > 0 )
            bytesize += vertices.length * Coord.BYTESIZE;
        return bytesize;
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        int vertices_length, idx;

        super.writeObject( outs );

        vertices_length = (short) vertices.length;
        outs.writeShort( vertices_length );
        for ( idx = 0; idx < vertices_length; idx++ )
            vertices[ idx ].writeObject( outs );
    }

    public Primitive( MixedDataInput ins )
    throws java.io.IOException
    {
        super();     // InfoBox();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        short idx, Nvertices;

        super.readObject( ins );

        Nvertices  = ins.readShort();
        vertices   = new Coord[ Nvertices ];
        for ( idx = 0; idx < vertices.length; idx++ )
            vertices[ idx ] = new Coord( ins );
        last_vtx_idx  = vertices.length - 1;

        // Determine the SuperClass, TimeBoundingBox.
        super.affectTimeBounds( vertices );
    }

    /*
        Vertices related operation:
    */

    // 0 <= vertex_order < vertices.length
    public void setVertex( int vertex_idx, final Coord vertex )
    throws ArrayIndexOutOfBoundsException
    {
        if ( vertex_idx < 0 || vertex_idx >= vertices.length ) {
            throw new ArrayIndexOutOfBoundsException( "input index, "
                                                    + vertex_idx
                                                    + ", is out of range, [0.."
                                                    + vertices.length + "]." );
        }
        vertices[ vertex_idx ] = vertex;
        super.affectTimeBounds( vertex );
    }

    public Coord getVertex( int vertex_idx )
    throws ArrayIndexOutOfBoundsException
    {
        if ( vertex_idx < 0 || vertex_idx >= vertices.length ) {
            throw new ArrayIndexOutOfBoundsException( "input index, "
                                                    + vertex_idx
                                                    + ", is out of range, [0.."
                                                    + vertices.length + "]." );
        }
        return vertices[ vertex_idx ];
    }

    public void setVertices( final Coord[] in_vertices )
    throws IllegalArgumentException
    {
        if ( in_vertices.length != vertices.length ) {
            throw new IllegalArgumentException( "input array size, "
                                              + in_vertices.length + ", is "
                                              + "different from the original, "
                                              + vertices.length );
        }
        vertices = in_vertices;
        super.affectTimeBounds( vertices );
    }

    public Coord[] getVertices()
    {
        return vertices;
    }

    //  API to support Shadow generation
    public List getListOfVertexLineIDs()
    {
        List lineIDs = new ArrayList( vertices.length );
        for ( int idx = 0; idx < vertices.length; idx++ )
            lineIDs.add( new Integer( vertices[ idx ].lineID ) );
        return lineIDs;
    }

    /*  Abstract method of Drawable  */
    //  Used to generate IdentityLineIDMap
    public Integer[] getArrayOfLineIDs()
    {
        Integer[] lineIDs = new Integer[ vertices.length ];
        for ( int idx = 0; idx < vertices.length; idx++ )
            lineIDs[ idx ] = new Integer( vertices[ idx ].lineID );
        return lineIDs;
    }

    /*
        setStartVertex()/setFinalVertex()/getStartVertex()/getFinalVertex()
        are stilll good for Primitive with only ONE vertex, like a event marker.
    */
    public void setStartVertex( final Coord start_vtx )
    {
        vertices[ 0 ] = start_vtx;
        super.affectTimeBounds( start_vtx );
    }

    public void setFinalVertex( final Coord final_vtx )
    {
        vertices[ last_vtx_idx ] = final_vtx;
        super.affectTimeBounds( final_vtx );
    }

    public Coord getStartVertex()
    {
        return vertices[ 0 ];
    }

    public Coord getFinalVertex()
    {
        return vertices[ last_vtx_idx ];
    }

    public String toString()
    {
        StringBuffer rep;
        int idx;

        rep = new StringBuffer( "Primitive[ " + super.toString() + " " );
        for ( idx = 0; idx < vertices.length; idx++ )
            rep.append( vertices[ idx ].toString() + " " );
        rep.append( "]" );
        rep.append( " bsize=" + this.getByteSize() );
        return rep.toString();
    }

/*
    // Primitive( final Coord[] ) and Primitive( Category, final Coord[] )
    // are VERY dangerous constructors because only references of Coord[]
    // are passed, so Coord[] of originating Primitive could be modified
    // in multiple objects!!!!!
    public Primitive( final Coord[] in_vertices )
    {
        super( in_vertices );
        vertices        = in_vertices;
        last_vtx_idx    = vertices.length - 1;
        super.setCategory( null );
    }

    public Primitive( Category in_type, final Coord[] in_vertices )
    {
        super( in_vertices );
        vertices        = in_vertices;
        last_vtx_idx    = vertices.length - 1;
        super.setCategory( in_type );
    }

    //  Check clone() interface
    public static final void main( String[] args )
    {
        Primitive prime, sobj;

        Category ctgy = new Category();  // incomplete category
        ctgy.setInfoKeys( "msg_tag\nmsg_size\n" );
        prime = new Primitive( ctgy, new Coord[] { new Coord( 1.1, 1 ),
                                                 new Coord( 2.2, 2 ) } );
        prime.setInfoValue( 0, 10 );
        prime.setInfoValue( 1, 1024 );
        System.out.println( "prime = " + prime );

        sobj = null;
        try {
            sobj = (Primitive) prime.clone();
        } catch( CloneNotSupportedException cerr ) {
            cerr.printStackTrace();
            System.exit( 1 );
        }

        System.out.println( "\nAfter cloning" );
        System.out.println( "prime = " + prime );
        System.out.println( "sobj = " + sobj );

        sobj.getStartVertex().time = 4.4;
        sobj.getFinalVertex().time = 5.5;
        sobj.setInfoValue( 0, 1000 );
        System.out.println( "\nAfter modification of the clone" );
        System.out.println( "prime = " + prime );
        System.out.println( "sobj = " + sobj );
        System.out.println( "This proves that clone() is useless for Shadow" );
    }
*/


    public boolean isTimeOrdered()
    {
        if ( ! super.isTimeOrdered() ) {
            System.err.println( "**** Violation of Causality ****\n"
                              + "Offending Primitive -> " + this );
            return false;
        }
        for ( int idx = 0 ; idx <= last_vtx_idx ; idx++ ) {
            if ( ! super.contains( vertices[ idx ].time ) ) {
                System.err.println( "**** Out of Primitive Time Range ****\n"
                                  + "Offending Primitive -> " + this + "\n"
                                  + "\t time coordinate " + idx
                                  + " is out of range." );
                return false;
            }
        }
        return true;
    }



    // Implementation of abstract methods.

    /* 
        0.0f < nesting_ftr <= 1.0f
    */
    public  int  drawState( Graphics2D g, CoordPixelXform coord_xform,
                            Map map_line2row, DrawnBoxSet drawn_boxes,
                            ColorAlpha color )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = start_vtx.time;  /* different from Shadow */
        tFinal = final_vtx.time;  /* different form Shadow */

        int    rowID;
        float  nesting_ftr;
        /* assume RowID and NestingFactor have been calculated */
        rowID       = super.getRowID();
        nesting_ftr = super.getNestingFactor();

        // System.out.println( "\t" + this + " nestftr=" + nesting_ftr );
        
        float  rStart, rFinal;
        rStart = (float) rowID - nesting_ftr / 2.0f;
        rFinal = rStart + nesting_ftr;

        return State.draw( g, color, null, coord_xform,
                           drawn_boxes.getLastStatePos( rowID ),
                           tStart, rStart, tFinal, rFinal );
    }

    //  assume this Primitive overlaps with coord_xform.TimeBoundingBox
    public  int  drawArrow( Graphics2D g, CoordPixelXform coord_xform,
                            Map map_line2row, DrawnBoxSet drawn_boxes,
                            ColorAlpha color )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = start_vtx.time;
        tFinal = final_vtx.time;

        int    iStart, iFinal;
        iStart = ( (Integer)
                   map_line2row.get( new Integer(start_vtx.lineID) )
                 ).intValue();
        iFinal = ( (Integer)
                   map_line2row.get( new Integer(final_vtx.lineID) )
                 ).intValue();

        return Arrow.draw( g, color, null, coord_xform,
                           drawn_boxes.getLastArrowPos( iStart, iFinal ),
                           tStart, (float) iStart, tFinal, (float) iFinal );
    }

    public  int  drawEvent( Graphics2D g, CoordPixelXform coord_xform,
                            Map map_line2row, DrawnBoxSet drawn_boxes,
                            ColorAlpha color )
    {
        Coord  vtx;
        vtx = this.getStartVertex();

        double tPoint;
        tPoint = vtx.time;  /* different from Shadow */

        int    rowID;
        float  rPeak, rStart, rFinal;
        rowID  = ( (Integer)
                   map_line2row.get( new Integer(vtx.lineID) )
                 ).intValue();
        // rPeak  = (float) rowID + NestingStacks.getHalfInitialNestingHeight();
        rPeak  = (float) rowID - 0.25f;
        rStart = (float) rowID - 0.5f;
        rFinal = rStart + 1.0f;

        return Event.draw( g, color, null, coord_xform,
                           drawn_boxes.getLastEventPos( rowID ),
                           tPoint, rPeak, rStart, rFinal );
    }



    /* 
        0.0f < nesting_ftr <= 1.0f
    */
    public  boolean isPixelInState( CoordPixelXform coord_xform,
                                    Map map_line2row, Point pix_pt )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = start_vtx.time;  /* different from Shadow */
        tFinal = final_vtx.time;  /* different form Shadow */

        int    rowID;
        float  nesting_ftr;
        /*
        rowID  = ( (Integer)
                   map_line2row.get( new Integer(start_vtx.lineID) )
                 ).intValue();
        */
        /* assume RowID and NestingFactor have been calculated */
        rowID       = super.getRowID();
        nesting_ftr = super.getNestingFactor();

        // System.out.println( "\t" + this + " nestftr=" + nesting_ftr );

        float  rStart, rFinal;
        rStart = (float) rowID - nesting_ftr / 2.0f;
        rFinal = rStart + nesting_ftr;

        return State.containsPixel( coord_xform, pix_pt,
                                    tStart, rStart, tFinal, rFinal );
    }

    //  assume this Primitive overlaps with coord_xform.TimeBoundingBox
    public  boolean isPixelOnArrow( CoordPixelXform coord_xform,
                                    Map map_line2row, Point pix_pt )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = start_vtx.time;
        tFinal = final_vtx.time;

        float  rStart, rFinal;
        rStart = ( (Integer)
                   map_line2row.get( new Integer(start_vtx.lineID) )
                 ).floatValue();
        rFinal = ( (Integer)
                   map_line2row.get( new Integer(final_vtx.lineID) )
                 ).floatValue();

        return Line.containsPixel( coord_xform, pix_pt,
                                   tStart, rStart, tFinal, rFinal );
    }

    public  boolean isPixelAtEvent( CoordPixelXform coord_xform,
                                    Map map_line2row, Point pix_pt )
    {
        Coord  vtx;
        vtx = this.getStartVertex();

        double tPoint;
        tPoint = vtx.time;  /* different from Shadow */

        int    rowID;
        float  rStart, rFinal;
        rowID  = ( (Integer)
                   map_line2row.get( new Integer(vtx.lineID) )
                 ).intValue();
        rStart = (float) rowID - 0.5f;
        rFinal = rStart + 1.0f;

        return Event.containsPixel( coord_xform, pix_pt,
                                    tPoint, rStart, rFinal );
    }

    public boolean containSearchable()
    {
        return super.getCategory().isVisiblySearchable();
    }
}

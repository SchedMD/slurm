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
import java.util.Set;
import java.util.SortedSet;
import java.util.TreeSet;
import java.util.Iterator;
import java.util.Arrays;
import java.io.ByteArrayInputStream;

import base.io.MixedDataInputStream;
import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
import base.topology.Line;
import base.topology.Arrow;
import base.topology.State;
import base.topology.Event;

// Composite should be considered as an InfoBox of Primitive[]

// Cloneable interface is for the creation of Shadow. 
public class Composite extends Drawable
                       implements MixedDataIO
//                       implements Cloneable
{
    private static final int INIT_BYTESIZE   = 2  /* primes.length */ ; 

    private   Primitive[]      primes;
    private   int              last_prime_idx;

    public Composite()
    {
        super();
        primes     = null;
    }

    public Composite( int Nprimes )
    {
        super();
        primes          = new Primitive[ Nprimes ];
        last_prime_idx  = primes.length - 1;
    }

    public Composite( Category in_type, int Nprimes )
    {
        super( in_type );
        primes          = new Primitive[ Nprimes ];
        last_prime_idx  = primes.length - 1;
    }

    //  This is NOT a copy constructor,
    //  only Category and InfoType[] are copied, not InfoValue[].
/*
    public Composite( final Composite cmplx )
    {
        super( cmplx );
        Primitive[] cmplx_primes = cmplx.primes;
        primes        = new Primitive[ cmplx_primes.length ];
        for ( int idx = 0; idx < primes.length; idx++ )
            primes[ idx ] = new Primitive( cmplx_primes[ idx ] );
        last_prime_idx    = primes.length - 1;
    }

    public Composite( Category in_type, final Composite cmplx )
    {
        super( in_type, cmplx );
        Primitive[] cmplx_primes = cmplx.primes;
        primes        = new Primitive[ cmplx_primes.length ];
        for ( int idx = 0; idx < primes.length; idx++ )
            primes[ idx ] = new Primitive( cmplx_primes[ idx ] );
        last_prime_idx    = primes.length - 1;
    }
*/

    //  For the support of JNI used in TRACE-API
    public Composite( int type_idx, double starttime, double endtime,
                      Primitive[] in_primes, byte[] byte_infovals )
    {
        super( type_idx, byte_infovals );
        super.setEarliestTime( starttime );
        super.setLatestTime( endtime );
        primes = in_primes;
    }

    //  For SLOG-2 Input/Output APIs: BufForDrawables.readObject()/TraceToSlog2
    public boolean resolveCategory( final Map categorymap )
    {
        Primitive prime;
        boolean  allOK, isOK;
        allOK = super.resolveCategory( categorymap );
        // InfoBox.resolveCategory( Map )
        if ( primes != null )
            for ( int idx = primes.length-1; idx >= 0; idx-- ) {
                 prime = primes[ idx ];
                 if ( prime != null ) {
                     isOK  = prime.resolveCategory( categorymap );
                     allOK = allOK && isOK;
                 }
            }
        return allOK;
    }   

/*
    public void setInfoValues()
    {
        Primitive prime;

        super.setInfoValues();
        if ( primes != null )
            for ( int idx = primes.length-1; idx >= 0; idx-- ) {
                 prime = primes[ idx ];
                 if ( prime != null )
                     prime.setInfoValues();
            }
    }
*/

    public int getNumOfPrimitives()
    {
        if ( primes != null )
            return primes.length;
        else
            return 0;
    }

    public int getByteSize()
    {
        int bytesize;
        //  Match the disk storage strategy in
        //  slog2.output.TreeNode.add( Drawable )
        //  if ( Category in Composite )
        //     => save the whole Composite,
        //     => will invoke Composite.writeObject()
        //  if ( No Category in Composite )
        //     => save the individual Primitive[],
        //     => will NOT invoke Composite.writeObject()
        if ( super.getCategory() != null ) 
            bytesize = super.getByteSize() + INIT_BYTESIZE;
        else
            bytesize = 0;

        if ( primes != null )
            for ( int idx = primes.length-1; idx >= 0; idx-- )
                bytesize += primes[ idx ].getByteSize();
        return bytesize;
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        int primes_length, idx;

        // Save the Lists in Increasing Starttime order, ie drawing order.
        Arrays.sort( primes, Drawable.INCRE_STARTTIME_ORDER );

        super.writeObject( outs );

        primes_length = (short) primes.length;
        outs.writeShort( primes_length );
        for ( idx = 0; idx < primes_length; idx++ )
            primes[ idx ].writeObject( outs );
    }

    public Composite( MixedDataInput ins )
    throws java.io.IOException
    {
        super();     // InfoBox();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        Primitive  prime;
        short     idx, Nprimes;

        super.readObject( ins );

        Nprimes  = ins.readShort();
        primes   = new Primitive[ Nprimes ];
        for ( idx = 0; idx < primes.length; idx++ ) {
            prime         = new Primitive( ins );
            prime.setParent( this );
            primes[ idx ] = prime;
            // Determine the SuperClass, TimeBoundingBox.
            super.affectTimeBounds( prime );
        }
        last_prime_idx  = primes.length - 1;
    }

    /*
        Primitives related operation:
    */

    // 0 <= prime_order < primes.length
    public void setPrimitive( int prime_idx, final Primitive prime )
    throws ArrayIndexOutOfBoundsException
    {
        if ( prime_idx < 0 || prime_idx >= primes.length ) {
            throw new ArrayIndexOutOfBoundsException( "input index, "
                                                    + prime_idx
                                                    + ", is out of range, [0.."
                                                    + primes.length + "]." );
        }
        primes[ prime_idx ] = prime;
        super.affectTimeBounds( prime );
    }

    public Primitive getPrimitive( int prime_idx )
    throws ArrayIndexOutOfBoundsException
    {
        if ( prime_idx < 0 || prime_idx >= primes.length ) {
            throw new ArrayIndexOutOfBoundsException( "input index, "
                                                    + prime_idx
                                                    + ", is out of range, [0.."
                                                    + primes.length + "]." );
        }
        return primes[ prime_idx ];
    }

    public void setPrimitives( final Primitive[] in_primes )
    throws IllegalArgumentException
    {
        if ( in_primes.length != primes.length ) {
            throw new IllegalArgumentException( "input array size, "
                                              + in_primes.length + ", is "
                                              + "different from the original, "
                                              + primes.length );
        }
        primes = in_primes;
        for ( int idx = primes.length-1; idx >= 0; idx-- )
            super.affectTimeBounds( primes[ idx ] );
    }

    public Primitive[] getPrimitives()
    {
        return primes;
    }

    public Iterator timeLapIterator( final TimeBoundingBox  tframe )
    {
        return new ItrOfPrimes( tframe );
    }

    //  API to support Shadow generation ??????
    public SortedSet getSetOfPrimitiveLineIDs()
    {
        SortedSet lineIDs = new TreeSet();
        for ( int idx = 0; idx < primes.length; idx++ )
            lineIDs.addAll( primes[ idx ].getListOfVertexLineIDs() );
        return lineIDs;
    }

    //  Used to generate IdentityLineIDMap  ?????? 
    public Integer[] getArrayOfLineIDs()
    {
        SortedSet lineIDset = this.getSetOfPrimitiveLineIDs();
        Integer[] lineIDary = new Integer[ lineIDset.size() ];
        lineIDset.toArray( lineIDary );
        return lineIDary;
    }

    /*
        setStartPrimitive()/setFinalPrimitive()
       /getStartPrimitive()/getFinalPrimitive()
       are stilll good for Composite with only ONE primitive.
    */
    public void setStartPrimitive( final Primitive start_prime )
    {
        primes[ 0 ] = start_prime;
        super.affectTimeBounds( start_prime );
    }

    public void setFinalPrimitive( final Primitive final_prime )
    {
        primes[ last_prime_idx ] = final_prime;
        super.affectTimeBounds( final_prime );
    }

/*
    public Primitive getStartPrimitive()
    {
        return primes[ 0 ];
    }

    public Primitive getFinalPrimitive()
    {
        return primes[ last_prime_idx ];
    }
*/

    /*
         getStartVertex()/getFinalVertex() defines the DrawOrderComparator.
    */
    public Coord getStartVertex()
    {
        return primes[ 0 ].getStartVertex();
    }

    public Coord getFinalVertex()
    {
        return primes[ last_prime_idx ].getFinalVertex();
    }

    public String toString()
    {
        StringBuffer rep;
        int idx;

        rep = new StringBuffer( "Composite[ " + super.toString() + " " );
        for ( idx = 0; idx < primes.length; idx++ )
            rep.append( primes[ idx ].toString() + " " );
        rep.append( "]" );
        return rep.toString();
    }



    private class ItrOfPrimes implements Iterator
    {
        private TimeBoundingBox  timeframe;
        private Primitive        next_primitive;
        private int              next_prime_idx;

        public ItrOfPrimes( final TimeBoundingBox  tframe )
        {
            timeframe       = tframe;

            next_prime_idx  = 0;
            if ( primes != null )
                next_primitive  = this.getNextInQueue();
            else 
                next_primitive  = null;
        }

        private Primitive getNextInQueue()
        {
            Primitive  next_prime;
            while ( next_prime_idx < primes.length ) {
                if ( primes[ next_prime_idx ].overlaps( timeframe ) ) {
                    next_prime  = primes[ next_prime_idx ];
                    next_prime_idx++;
                    return next_prime;
                }
            }
            return null;
        }

        public boolean hasNext()
        {
            return next_primitive != null;
        }

        public Object next()
        {
            Primitive        returning_prime;

            returning_prime = next_primitive;
            next_primitive  = this.getNextInQueue();  

            return returning_prime;
        }

        public void remove() {}
    }   // private class ItrOfPrimes 



    public boolean isTimeOrdered()
    {
        Primitive  prime;
        int        primes_length, idx;
        if ( ! super.isTimeOrdered() ) {
            System.err.println( "**** Violation of Causality ****\n"
                              + "Offending Composite -> " + this );
            return false;
        }
        primes_length = (short) primes.length;
        for ( idx = 0; idx < primes_length; idx++ ) {
            prime = primes[ idx ];
            if ( ! prime.isTimeOrdered() ) {
                System.err.println( "**** Internal Primitive Error ****\n"
                                  + "It is number " + idx + " primitive "
                                  + "in the composite." );
                return false;
            }
            if ( ! super.covers( prime ) ) {
                System.err.println( "**** Out of Composite Time Range ****\n"
                                  + "Offending Primitive -> " + this + "\n"
                                  + "\t time coordinate " + idx
                                  + " is out of range." );
                return false;
            }
        }
        return true;
    }

/*
    public void addPrimitivesToSet( Set prime_set )
    {
        int        primes_length, idx;
        primes_length = (short) primes.length;
        // primes[] is iterated in increaing starttime order
        for ( idx = 0; idx < primes_length; idx++ )
            prime_set.add( primes[ idx ] );
    }
*/

    public void addPrimitivesToSet( Set prime_set, TimeBoundingBox timeframe )
    {
        Primitive  prime;
        int        primes_length, idx;
        primes_length = (short) primes.length;
        // primes[] is iterated in increaing starttime order
        for ( idx = 0; idx < primes_length; idx++ ) {
            prime = primes[ idx ];
            if ( prime.overlaps( timeframe ) )
                prime_set.add( prime );
        }
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

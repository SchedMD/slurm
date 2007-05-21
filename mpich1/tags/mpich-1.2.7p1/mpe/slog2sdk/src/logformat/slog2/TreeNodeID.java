/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Comparator;
import java.io.DataOutput;
import java.io.DataInput;

import base.io.DataIO;

public class TreeNodeID implements DataIO // , Comparable
{
    public static final int BYTESIZE = 2  /* depth */
                                     + 4  /* xpos  */ ;

    public static final Order  INCRE_INDEX_ORDER = new IncreasingIndexOrder();
    public static final Order  DECRE_INDEX_ORDER = new DecreasingIndexOrder();

    public short depth;
    public int   xpos;

    public TreeNodeID( short in_depth, int in_xpos )
    {
        depth  = in_depth;
        xpos   = in_xpos;
    }

    public TreeNodeID( final TreeNodeID ID )
    {
        depth  = ID.depth;
        xpos   = ID.xpos;
    }

    public TreeNodeID getParentNodeID( int num_leafs )
    {
        return new TreeNodeID( (short)( depth+1 ), xpos/num_leafs );
    }


    /*
       Use of toParent(), toNextSibling() or toPreviousSibling() avoids
       creation of new TreeNodeID.  Interfaces like getParent(), 
       getNextSibling() and getPreviousSibling() may involve creation
       of new instance of TreeNodeID.
    */
    public void toParent( int num_leafs )
    {
        depth++;
        xpos /= num_leafs ;
    }

    public void toNextSibling()
    {
        xpos++;
    }

    public void toPreviousSibling()
    {
        xpos--;
    }

    public boolean isPossibleRoot()
    {
        return ( xpos == 0 );
    }

    public boolean isLeaf()
    {
        return ( depth == 0 );
    }

    public boolean equals( final TreeNodeID nodeID )
    {
        return ( this.depth == nodeID.depth && this.xpos == nodeID.xpos );
    }

    /*
    // If obj.getClass() != TreeNodeID.class, throws ClassCastException
    public int compareTo( Object obj )
    {
        return this.compareTo( (TreeNodeID) obj );
    }

    // Define the "natural ordering" imposed by Comparable used in SortedMap
    // The ordering here needs to be consistent with the primary time order
    // (i.e. increasing or decreasing part not the starttime or finaltime part)
    // defined in storage order of the drawable in the logfile, i.e.
    // Drawable.INCRE_STARTTIME_ORDER which is first determined by increasing
    // startime then decreasing finaltime order.
    public int compareTo( final TreeNodeID ID )
    {
        return INCRE_INDEX_ORDER.compare( this, ID );
    }
    */

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeShort( depth );
        outs.writeInt( xpos );
    }

    public TreeNodeID( DataInput ins )
    throws java.io.IOException
    {
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        depth  = ins.readShort();
        xpos   = ins.readInt();
    }

    public String toString()
    {
        return( "ID(" + depth + "," + xpos + ")" );
    }

    /*
        Define TreeNodeID.Order as an alias of java.util.Comparator
    */
    public interface Order extends Comparator
    {
        public boolean isIncreasingIndexOrdered();
    }

    /*
       Define the "natural ordering" imposed by Comparable used in SortedMap
       The ordering here needs to be consistent with the primary time order
       (i.e. increasing or decreasing part not the starttime or finaltime part)
       defined in storage order of the drawable in the logfile, i.e.
       Drawable.INCRE_STARTTIME_ORDER which is first determined by increasing
       startime then decreasing finaltime order.
    */
    private static class IncreasingIndexOrder implements Order
    {
        public int compare( Object o1, Object o2 )
        {
            TreeNodeID ID1, ID2;
            ID1  = (TreeNodeID) o1;
            ID2  = (TreeNodeID) o2;
            if ( ID1.depth == ID2.depth ) {
                if ( ID1.xpos == ID2.xpos )
                    return 0;
                else
                    return ( ID1.xpos < ID2.xpos ? -1 : 1 ); // increasing time
            }
            else
                return ( ID1.depth > ID2.depth ? -1 : 1 );
        }

        public boolean isIncreasingIndexOrdered() {return true;}
    }

    private static class DecreasingIndexOrder implements Order
    {
        public int compare( Object o1, Object o2 )
        {
            TreeNodeID ID1, ID2;
            ID1  = (TreeNodeID) o1;
            ID2  = (TreeNodeID) o2;
            if ( ID1.depth == ID2.depth ) {
                if ( ID1.xpos == ID2.xpos )
                    return 0;
                else
                    return ( ID1.xpos > ID2.xpos ? -1 : 1 ); // decreasing time
            }
            else
                return ( ID1.depth > ID2.depth ? -1 : 1 );
        }

        public boolean isIncreasingIndexOrdered() {return false;}
    }



    public final static void main( String[] args )
    {
        final short ll_max = 4;
              short dd;
              short ll;
              int   xp, xp_max;

        java.util.Map map = new java.util.TreeMap();
        for ( ll = ll_max; ll >=0 ; ll-- ) {
            dd = (short) ( ll_max - ll );
            xp_max = (int) Math.pow( (double) 2, (double) ll );
            for ( xp = xp_max-1; xp >= 0; xp-- )
                map.put( new TreeNodeID( dd, xp ),
                         new String( "_" + ll + ", " + xp + "_" ) );
        }

        TreeNodeID         ID;
        java.util.Iterator itr;
        itr = map.entrySet().iterator();
        while ( itr.hasNext() ) {
            ID = (TreeNodeID) ( (java.util.Map.Entry) itr.next() ).getKey();
            if ( ID.isPossibleRoot() ) 
                System.out.println( "\n" + ID );
            else
                System.out.println( ID );
        }
        System.out.println("\n\n" );

        java.util.Set set;
        set = new java.util.TreeSet( TreeNodeID.INCRE_INDEX_ORDER );
        for ( ll = 0 ; ll <= ll_max; ll++ ) {
            dd = (short) ( ll_max - ll );
            xp_max = (int) Math.pow( (double) 2, (double) ll );
            for ( xp = xp_max-1; xp >= 0; xp-- )
                set.add( new TreeNodeID( dd, xp ) );
        }

        itr = set.iterator();
        while ( itr.hasNext() ) {
            ID = (TreeNodeID) itr.next();
            if ( ID.isPossibleRoot() )
                System.out.println( "\n" + ID );
            else
                System.out.println( ID );
        }
        System.out.println("\n\n" );

        set = new java.util.TreeSet( TreeNodeID.DECRE_INDEX_ORDER );
        for ( ll = 0 ; ll <= ll_max; ll++ ) {
            dd = (short) ( ll_max - ll );
            xp_max = (int) Math.pow( (double) 2, (double) ll );
            for ( xp = xp_max-1; xp >= 0; xp-- )
                set.add( new TreeNodeID( dd, xp ) );
        }

        itr = set.iterator();
        while ( itr.hasNext() ) {
            ID = (TreeNodeID) itr.next();
            if ( ID.isPossibleRoot() )
                System.out.println( ID + "\n" );
            else
                System.out.println( ID );
        }
    }
}

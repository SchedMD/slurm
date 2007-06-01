/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Comparator;
import java.util.Collection;
import java.util.Iterator;
import java.io.DataOutput;
import java.io.DataInput;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import base.drawable.Primitive;

public abstract class BufForObjects extends TimeBoundingBox
//                                    implements Comparable
{
    protected static final int  BYTESIZE = TimeBoundingBox.BYTESIZE
                                         + TreeNodeID.BYTESIZE
                                         + FileBlockPtr.BYTESIZE;

    public static final Order  INCRE_INDEX_ORDER
                               = new Order( TreeNodeID.INCRE_INDEX_ORDER );
    public static final Order  DECRE_INDEX_ORDER
                               = new Order( TreeNodeID.DECRE_INDEX_ORDER );

    private TreeNodeID        ID;
    private FileBlockPtr      blockptr;


    // The constructor can be involved from subclass
    protected BufForObjects()
    {
        super();
        ID                   = new TreeNodeID( (short) 0, 0 );
        blockptr             = new FileBlockPtr();
    }

    // For TreeNodeID type

    public void setTreeNodeID( final TreeNodeID new_ID )
    {
        ID.depth  = new_ID.depth;
        ID.xpos   = new_ID.xpos;
    }

    public TreeNodeID getTreeNodeID()
    {
        return ID;
    }

    public boolean isLeaf()
    {
        return ID.isLeaf();
    }

    public boolean isPossibleRoot()
    {
        return ID.isPossibleRoot();
    }

    /*
    // if obj.getClass() != BufForObjects.class, throws ClassCastException
    public int compareTo( Object obj )
    {
        return compareTo( (BufForObjects) obj );
    }

    // Define the "natural ordering", i.e. Comparable interface,
    // as needed by SortedMap and SortedSet.
    // The natural ordering of the BufForObjects is the same as
    // the natural ordering of the TreeNodeID stored within.
    public int compareTo( BufForObjects buf4objs )
    {
        return ID.compareTo( buf4objs.ID );
    }
    */

    // For FileBlockPtr type

    public void setFileBlockPtr( long in_fptr, int in_size )
    {
        blockptr.setFileBlockPtr( in_fptr, in_size );
    }

    public FileBlockPtr getFileBlockPtr()
    {
        return blockptr;
    }

    public long getFilePointer()
    {
        return blockptr.getFilePointer();
    }

    public int  getBlockSize()
    {
        return blockptr.getBlockSize();
    }

    public abstract int  getByteSize();

    public abstract void add( final Primitive prime );

    public abstract void empty();

    public abstract int  getNumOfDrawables();

    public abstract int  getNumOfPrimitives();

    // Iterator of Nestable Drawables in Decreasing Endtime order
    public abstract
    Iterator nestableBackIterator( final TimeBoundingBox tframe,
                                         boolean         isComposite );

    // Iterator of Nestless Drawables in Decreasing Endtime order
    public abstract
    Iterator nestlessBackIterator( final TimeBoundingBox tframe );

    // Iterator of Nestable Drawables in Increasing Endtime order
    public abstract
    Iterator nestableForeIterator( final TimeBoundingBox tframe,
                                         boolean         isComposite );

    // Iterator of Nestless Drawables in Increasing Endtime order
    public abstract
    Iterator nestlessForeIterator( final TimeBoundingBox tframe );

    public abstract LineIDMap getIdentityLineIDMap();

    protected static LineIDMap toIdentityLineIDMap( Collection obj_coll )
    {
        Drawable   dobj;
        Integer[]  lineIDs;
        int        idx;
        LineIDMap  lineIDmap = new LineIDMap( 1 );
        Iterator objs_itr = obj_coll.iterator();
        while ( objs_itr.hasNext() ) {
            dobj = (Drawable) objs_itr.next();
            lineIDs = dobj.getArrayOfLineIDs();
            for ( idx = 0; idx < lineIDs.length; idx++ ) {
                lineIDmap.put( lineIDs[ idx ],
                               new Integer[]{ lineIDs[ idx ] } );
            }
        }
        return lineIDmap;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        super.writeObject( outs );   // TimeBoundingBox.writeObject( outs )
        ID.writeObject( outs );
        blockptr.writeObject( outs );
    }

    // The constructor can be involved from subclass
    /*
    protected BufForObjects( DataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }
    */

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        super.readObject( ins );   // TimeBoundingBox.readObject( ins )
        ID.readObject( ins );
        blockptr.readObject( ins );
    }

    public String toString()
    {
        return ( ID.toString()        /* TreeNodeID */ + " "
               + super.toString()     /* TimeBoundingBox */ + " "
               + blockptr.toString()  /* FileBlockPtr */ );
    }



    public static class Order implements TreeNodeID.Order
    {
        private TreeNodeID.Order  treenode_order;

        public Order( final TreeNodeID.Order tnode_order )
        {
            treenode_order = tnode_order;
        }

        public boolean isIncreasingIndexOrdered()
        {
            return treenode_order.isIncreasingIndexOrdered();
        }

        public int compare( Object o1, Object o2 )
        {
            BufForObjects  buf4objs1, buf4objs2;
            buf4objs1 = (BufForObjects) o1;
            buf4objs2 = (BufForObjects) o2;
            return treenode_order.compare( buf4objs1.ID, buf4objs2.ID );
        }
    }

}

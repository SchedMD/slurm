/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.input;

import java.util.Map;
import java.util.Iterator;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
import base.drawable.*;
import logformat.slog2.*;

public class TreeNode extends BufForDrawables
//                      implements MixedDataIO
{
    // Array of BufForShadows for this node
    private BufForShadows[]  shadowbufs;

    public TreeNode()
    {
        super( false );
        shadowbufs   = null;
    }

    //  ChildStub here refers to the Signature of the ChildNode,
    //  i.e. ChildNode minus all drawables and shadows, i.e. BufForObject
    //  "final" keyword protects modification of content of BufForShadows[].
    public BufForObjects[] getChildStubs()
    {
        return shadowbufs;
    }

    public int getNodeByteSize()
    {
        int             total_bytesize;  // bytesize for the disk footprint

        total_bytesize = super.getByteSize()
                       + 4  /* childnode.shadowbufs.size() */ ;
        if ( shadowbufs != null ) {
            for ( int idx = 0; idx < shadowbufs.length; idx++ )
                total_bytesize += shadowbufs[ idx ].getByteSize();
        }
        return total_bytesize;
    }

    public Iterator iteratorOfDrawables( final TimeBoundingBox  tframe,
                                         final Drawable.Order   dobj_order,
                                               boolean          isComposite,
                                               boolean          isNestable )
    {
        boolean isForeItr;
        isForeItr = dobj_order.isIncreasingTimeOrdered();
        if ( isForeItr ) {
            if ( isNestable )
                return super.nestableForeIterator( tframe, isComposite );
            else
                return super.nestlessForeIterator( tframe );
        }
        else {
            if ( isNestable )
                return super.nestableBackIterator( tframe, isComposite );
            else
                return super.nestlessBackIterator( tframe );
        }
    }

    public Iterator iteratorOfShadows( final TimeBoundingBox  tframe,
                                       final Drawable.Order   dobj_order,
                                             boolean          isNestable )
    {
        boolean isForeItr;
        isForeItr = dobj_order.isIncreasingTimeOrdered();
        if ( isForeItr ) {
            if ( isNestable )
                return new ForeItrOfNestableShadows( tframe );
            else
                return new ForeItrOfNestlessShadows( tframe );
        }
        else {
            if ( isNestable )
                return new BackItrOfNestableShadows( tframe );
            else
                return new BackItrOfNestlessShadows( tframe );
        }
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        // Empty interface to fulfill MixedDataIO
        System.err.println( "ERROR! : slog2.input.TreeNode.writeObject() "
                          + "should NOT be called!" );
    }

/*
    public TreeNode( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        super.readObject( ins );  // BufForDrawable.readObject( ins );
        int Nbufs = ins.readInt();
        if ( Nbufs > 0 ) {
            shadowbufs = new BufForShadows[ Nbufs ];
            for ( int idx = 0; idx < shadowbufs.length; idx++ )
                shadowbufs[ idx ] = new BufForShadows( ins );
        }
        else
            shadowbufs = null;
    }
*/

    public TreeNode( MixedDataInput ins, final Map categorymap )
    throws java.io.IOException
    {
        this();
        this.readObject( ins, categorymap );
    }

    public void readObject( MixedDataInput ins, final Map categorymap )
    throws java.io.IOException
    {
        // BufForDrawable.readObject( ins, categorymap );
        super.readObject( ins, categorymap );

        int Nbufs = ins.readInt();
        if ( Nbufs > 0 ) {
            shadowbufs = new BufForShadows[ Nbufs ];
            for ( int idx = 0; idx < shadowbufs.length; idx++ )
                shadowbufs[ idx ] = new BufForShadows( ins, categorymap );
        }
        else
            shadowbufs = null;
    }

    public void reorderDrawables( final Drawable.Order dobj_order )
    {
        super.reorderDrawables( dobj_order );
        if ( shadowbufs != null )
            for ( int idx = 0; idx < shadowbufs.length; idx++ )
                shadowbufs[ idx ].reorderDrawables( dobj_order );
    }
 
    public String toString()
    {
        StringBuffer rep = new StringBuffer( "\t*** Start of TreeNode ***\n" );
        rep.append( super.toString() + "\n" );
        if ( shadowbufs != null ) {
            for ( int idx = 0; idx < shadowbufs.length; idx++ ) {
                rep.append( "\t BufForShadows No. " + idx + "\n" );
                rep.append( shadowbufs[ idx ] );
            }
        }
        rep.append( "\t*** End of TreeNode ***\n" );
        return rep.toString();
    }



    private class BackItrOfNestableShadows extends IteratorOfGroupObjects
    {
        // IS_COMPOSITE can be true/false, because
        // shadowbuf.nestableBackIterator() does not use IS_COMPOSITE
        private static final  boolean  IS_COMPOSITE  = false;

        private int              shadowbufs_length;
        private int              next_buf_idx;

        public BackItrOfNestableShadows( final TimeBoundingBox  tframe )
        {
            super( tframe );
            if ( shadowbufs != null )
                shadowbufs_length  = shadowbufs.length;
            else
                shadowbufs_length  = 0;
            next_buf_idx       = shadowbufs_length - 1;
            super.setObjGrpItr( this.nextObjGrpItr( tframe ) );
        }

        protected Iterator nextObjGrpItr( final TimeBoundingBox tframe )
        {
            BufForShadows    shadowbuf;

            if ( shadowbufs != null ) {
                while ( next_buf_idx >= 0 ) {
                    shadowbuf  = shadowbufs[ next_buf_idx ];
                    next_buf_idx--;
                    if ( shadowbuf.overlaps( tframe ) )
                        return shadowbuf.nestableBackIterator( tframe,
                                                               IS_COMPOSITE );
                }
            }
            // return NULL when no more shadowbuf in shadowbufs[]
            return null;
        }
    }

    private class BackItrOfNestlessShadows extends IteratorOfGroupObjects
    {
        private int              shadowbufs_length;
        private int              next_buf_idx;

        public BackItrOfNestlessShadows( final TimeBoundingBox  tframe )
        {
            super( tframe );
            if ( shadowbufs != null )
                shadowbufs_length  = shadowbufs.length;
            else
                shadowbufs_length  = 0;
            next_buf_idx       = shadowbufs_length - 1;
            super.setObjGrpItr( this.nextObjGrpItr( tframe ) );
        }

        protected Iterator nextObjGrpItr( final TimeBoundingBox tframe )
        {
            BufForShadows    shadowbuf;

            if ( shadowbufs != null ) {
                while ( next_buf_idx >= 0 ) {
                    shadowbuf  = shadowbufs[ next_buf_idx ];
                    next_buf_idx--;
                    if ( shadowbuf.overlaps( tframe ) )
                        return shadowbuf.nestlessBackIterator( tframe );
                }
            }
            // return NULL when no more shadowbuf in shadowbufs[]
            return null;
        }
    }

    private class ForeItrOfNestableShadows extends IteratorOfGroupObjects
    {
        // IS_COMPOSITE can be true/false, because
        // shadowbuf.nestableForeIterator() does not use IS_COMPOSITE
        private static final  boolean  IS_COMPOSITE  = false;

        private int              shadowbufs_length;
        private int              next_buf_idx;

        public ForeItrOfNestableShadows( final TimeBoundingBox  tframe )
        {
            super( tframe );
            if ( shadowbufs != null )
                shadowbufs_length  = shadowbufs.length;
            else
                shadowbufs_length  = 0;
            next_buf_idx       = 0;
            super.setObjGrpItr( this.nextObjGrpItr( tframe ) );
        }

        protected Iterator nextObjGrpItr( final TimeBoundingBox tframe )
        {
            BufForShadows    shadowbuf;

            if ( shadowbufs != null ) {
                while ( next_buf_idx < shadowbufs_length ) {
                    shadowbuf  = shadowbufs[ next_buf_idx ];
                    next_buf_idx++;
                    if ( shadowbuf.overlaps( tframe ) )
                        return shadowbuf.nestableForeIterator( tframe,
                                                               IS_COMPOSITE );
                }
            }
            // return NULL when no more shadowbuf in shadowbufs[]
            return null;
        }
    }

    private class ForeItrOfNestlessShadows extends IteratorOfGroupObjects
    {
        private int              shadowbufs_length;
        private int              next_buf_idx;

        public ForeItrOfNestlessShadows( final TimeBoundingBox  tframe )
        {
            super( tframe );
            if ( shadowbufs != null )
                shadowbufs_length  = shadowbufs.length;
            else
                shadowbufs_length  = 0;
            next_buf_idx       = 0;
            super.setObjGrpItr( this.nextObjGrpItr( tframe ) );
        }

        protected Iterator nextObjGrpItr( final TimeBoundingBox tframe )
        {
            BufForShadows    shadowbuf;

            if ( shadowbufs != null ) {
                while ( next_buf_idx < shadowbufs_length ) {
                    shadowbuf  = shadowbufs[ next_buf_idx ];
                    next_buf_idx++;
                    if ( shadowbuf.overlaps( tframe ) )
                        return shadowbuf.nestlessForeIterator( tframe );
                }
            }
            // return NULL when no more shadowbuf in shadowbufs[]
            return null;
        }
    }
}

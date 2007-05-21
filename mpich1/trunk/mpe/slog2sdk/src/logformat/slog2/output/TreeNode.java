/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.output;

import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;
import java.util.Iterator;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.drawable.*;
import logformat.slog2.*;

public class TreeNode extends BufForDrawables
{
    private List            shadowbufs;  // List of BufForShadows

    // Temporary data structurs for SLOG-2 Output API
    private boolean         haveShadowBufsBeenSaved;
    private BufForShadows   shadowbuf;
    private Map             shadowdefs_map;
    private TreeNode        childnode;

    public TreeNode()
    {
        super( true );
        shadowbufs                = new ArrayList();
        haveShadowBufsBeenSaved   = false;
        shadowbuf                 = null;
        shadowdefs_map            = null;
        childnode                 = null;
    }

    public TreeNode( final TreeNode previous_node )
    {
        this();
        childnode        = previous_node;
        // shadowdefs_map   = childnode.shadowdefs_map;
    }

    private void ensureNonNullShadowBuf()
    {
        if ( shadowbuf == null ) {
            shadowbuf = new BufForShadows( true );
            shadowbuf.setMapOfTopologyToShadowDef( shadowdefs_map );
        }
    }

    public void setTreeNodeID( final TreeNodeID new_ID )
    {
        super.setTreeNodeID( new_ID );
        ensureNonNullShadowBuf();
        shadowbuf.setTreeNodeID( new_ID );
    }

    public void setMapOfTopologyToShadowDef( Map in_shadefs )
    {
        shadowdefs_map = in_shadefs;
    }

    public void add( final Drawable dobj )
    {
        if ( dobj instanceof Composite ) {
            Composite cmplx = (Composite) dobj;
            Primitive[] primes = null;
            if ( cmplx.getCategory() != null ) {
                super.add( cmplx );            // BufForDrawables.add(Composite)
                ensureNonNullShadowBuf();
                primes = cmplx.getPrimitives();
                for ( int idx = 0; idx < primes.length; idx++ )
                   shadowbuf.add( primes[ idx ] );
            }
            else {
                ensureNonNullShadowBuf();
                primes = cmplx.getPrimitives();
                for ( int idx = 0; idx < primes.length; idx++ ) {
                   super.add( primes[ idx ] ); // BufForDrawables.add(Primitive)
                   shadowbuf.add( primes[ idx ] );
                }
            }
        }
        else { // if ( dobj instanceof Primitive )
            Primitive prime = (Primitive) dobj;
            super.add( prime );                // BufForDrawables.add(Primitive)
            ensureNonNullShadowBuf();
            shadowbuf.add( prime );
        }
    }

    public void empty()
    {
        if ( childnode != null && childnode.haveShadowBufsBeenSaved ) {
            Iterator belowbufs = childnode.shadowbufs.iterator();
            while ( belowbufs.hasNext() )
                ( (BufForShadows) belowbufs.next() ).empty();
            // renewShadowBufs() for others to use
            childnode.shadowbufs.clear();
            childnode.haveShadowBufsBeenSaved = false;
        }

        // try to clear() on BufForDrawables.buf4drawables
        super.empty();
    }

    /*
       finalizeLatestTime() should be invoked BEFORE
       mergeVerticalShadowBufs() and shiftHorizontalShadowBuf() are called
    */
    public void finalizeLatestTime( final Drawable  last_drawable_added )
    {
        if ( childnode == null ) // i.e. if ( super.isLeaf() )
            super.setLatestTime( last_drawable_added.getLatestTime() );
        else  // if ( childnode != null )
            super.setLatestTime( childnode.getLatestTime() );

        if ( shadowbuf != null ) {
            // After setting the LatestTime, seal the shadows' category weights
            shadowbuf.setLatestTime( super.getLatestTime() );
            shadowbuf.initializeMapOfCategoryWeights();
        }
    }

    /*
       mergeVerticalShadowBufs() should be called AFTER finalizeLatestTime()
       but BEFORE shiftHorizontalShadowBuf(), because the current shadowbuf
       needs to be updated with the childnode's shadowbufs[] before finalization
    */
    public void mergeVerticalShadowBufs()
    {
        BufForShadows   buf;
        if ( childnode != null ) {
            Iterator belowbufs = childnode.shadowbufs.iterator();
            while ( belowbufs.hasNext() ) {
                buf = (BufForShadows) belowbufs.next();
                shadowbuf.mergeWith( buf );   // shadowbuf += buf
                shadowbuf.affectTimeBounds( buf );

                /*
                System.out.println( "BufForShadows("
                                  + shadowbuf.getTreeNodeID() + ") += "
                                  + "BufForShadows("
                                  + buf.getTreeNodeID() + ")" );
                */
            }
        }
    }

    /*
       shiftHorizontalShadowBuf() should be called AFTER both
       mergeVerticalShadowBufs() and finalizeLatestTime().
    */
    public void shiftHorizontalShadowBuf()
    {
        if ( shadowbuf != null ) {
            shadowbuf.finalizeMapOfCategoryWeights();
            shadowbufs.add( shadowbuf );
            shadowbuf   = null;
        }
    }

    // TreeNode.affectEarliestTime() is invoked 
    // right after TreeNode.add() where TreeNode is "tree root".
    public void affectEarliestTime( double in_time )
    {
        super.affectEarliestTime( in_time );
        ensureNonNullShadowBuf();
        shadowbuf.affectEarliestTime( in_time );
    }

    // TreeNode.setEarliestTime() is invoked 
    // right after TreeNode.empty()
    public void setEarliestTime( double in_time )
    {
        super.setEarliestTime( in_time );
        ensureNonNullShadowBuf();
        shadowbuf.setEarliestTime( in_time );
    }

    public void setFileBlockPtr( long in_fptr, int in_size )
    {
        super.setFileBlockPtr( in_fptr, in_size );
        ensureNonNullShadowBuf();
        shadowbuf.setFileBlockPtr( in_fptr, in_size );
    }

    public int getNodeByteSize()
    {
        int             total_bytesize;  // bytesize for the disk footprint
        BufForShadows   buf;

        total_bytesize = super.getByteSize()
                       + 4  /* childnode.shadowbufs.size() */ ;
        // System.err.println( "TreeNode.NodeByteSize = " + total_bytesize );
        if ( childnode != null ) {
            Iterator belowbufs = childnode.shadowbufs.iterator();
            while ( belowbufs.hasNext() ) {
                buf = (BufForShadows) belowbufs.next();
                total_bytesize += buf.getByteSize();
            }
        }
        return total_bytesize;
    }

    public LineIDMap getIdentityLineIDMap()
    {
        BufForShadows  belowbuf;
        LineIDMap      lineIDmap = super.getIdentityLineIDMap();
        if ( childnode != null ) {
            Iterator belowbufs = childnode.shadowbufs.iterator();
            while ( belowbufs.hasNext() ) {
                belowbuf = (BufForShadows) belowbufs.next();
                lineIDmap.putAll( belowbuf.getIdentityLineIDMap() );
            }
        }
        lineIDmap.setTitle( "Identity Map" );
        lineIDmap.setColumnLabels( new String[] { "LineID" } );
        return lineIDmap;
    }

    //  This is only applicable to the ROOT treenode.
    public void summarizeCategories()
    {
        BufForShadows   buf;
        // Assume shiftHorizontalShadowBuf() has just been called,
        // so shadowbuf == null, otherwise this function needs to call
        // this.shiftHorizontalShadowBuf()
        if ( shadowbuf != null ) {
            throw new RuntimeException( "UnexpectedError: "
                                      + "shadowbuf is NOT NULL! Aborting..." );
        }

        if ( shadowbufs.size() != 1 ) {
            throw new RuntimeException( "UnexpectedError: "
                                      + "shadowbufs[]'s size is "
                                      + shadowbufs.size() + "! Aborting..." );
        }
            
        shadowbuf = (BufForShadows) shadowbufs.get( 0 );
        shadowbuf.summarizeCategories();
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        super.writeObject( outs );  // BufForDrawable.writeObject( outs );
        if ( childnode != null ) {
            outs.writeInt( childnode.shadowbufs.size() );
            Iterator belowbufs = childnode.shadowbufs.iterator();
            while ( belowbufs.hasNext() )
                ( (BufForShadows) belowbufs.next() ).writeObject( outs );
            // Mark the ShadowBufs in the children node has been saved
            childnode.haveShadowBufsBeenSaved = true;
        }
        else  // childnode == null
            outs.writeInt( 0 );
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
        for ( int idx = 0; idx < Nbufs; idx++ ) {
             shadowbufs.add( new BufForShadows( ins ) );
        }     
    }
*/

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "\t*** Start of TreeNode ***\n" );
        rep.append( super.toString() + "\n" );
        if ( childnode != null ) {
            BufForShadows belowbuf;
            Iterator belowbufs = childnode.shadowbufs.iterator();
            for ( int buf_idx = 1; belowbufs.hasNext(); buf_idx++ ) {
                belowbuf = (BufForShadows) belowbufs.next();
                rep.append( "\t BufForShadows No. " + buf_idx + "\n" );
                rep.append( belowbuf + "\n" );
            }
        }
        rep.append( "\t*** End of TreeNode ***\n" );
        return rep.toString();
    }

    public String toStringForInput()
    {
        StringBuffer rep = new StringBuffer( "\t*** Start of TreeNode ***\n" );
        rep.append( super.toString() + "\n" );

            BufForShadows sobj_buf;
            Iterator sobj_bufs = this.shadowbufs.iterator();
            for ( int buf_idx = 1; sobj_bufs.hasNext(); buf_idx++ ) {
                sobj_buf = (BufForShadows) sobj_bufs.next();
                rep.append( "\t BufForShadows No. " + buf_idx + "\n" );
                rep.append( sobj_buf + "\n" );
            }

        rep.append( "\t*** End of TreeNode ***\n" );
        return rep.toString();
    }
}

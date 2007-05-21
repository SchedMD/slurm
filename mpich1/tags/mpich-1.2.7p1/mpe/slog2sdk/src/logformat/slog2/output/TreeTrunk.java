/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.output;

import java.util.Map;
import java.util.List;
import java.util.LinkedList;
import java.util.Iterator;
import java.util.ListIterator;

import base.drawable.Drawable;
import logformat.slog2.*;

public class TreeTrunk
{
    private OutputLog   slog;
    private Map         shadowdefs_map;
    private int         num_leafs;
    private int         leaf_bytesize;

    private List        treenodes;
    private TreeNode    leaf, root;
    private Drawable    last_dobj_added;   // Last Drawable added to TreeTrunk

    private LineIDMap   lineIDmapOne;

    public TreeTrunk( OutputLog in_slog, Map in_shadefs )
    {
        // Initialize
        slog             = in_slog;
        shadowdefs_map   = in_shadefs;
        num_leafs        = (int) slog.getNumChildrenPerNode();
        leaf_bytesize    = slog.getTreeLeafByteSize();

        // Initialize "leaf" and "root" to the same TreeNode.
        leaf             = new TreeNode();
        leaf.setMapOfTopologyToShadowDef( shadowdefs_map );
        leaf.setTreeNodeID( new TreeNodeID( (short) 0, 0 ) );
        treenodes        = new LinkedList();
        treenodes.add( leaf );
        root             = leaf;

        last_dobj_added  = null;
        lineIDmapOne     = null;
    }

    public void addDrawable( final Drawable dobj )
    {
        TreeNode    node;
        boolean     hasDrawableBeenStored;


        // Check if the leaf of the trunk is full.  If so,
        // the trunk is switched (and/or elongated) to become the next branch
        if ( ( leaf.getNodeByteSize() + dobj.getByteSize() ) > leaf_bytesize )
            this.switchToNextBranch();

        hasDrawableBeenStored = false;
        Iterator nodes_itr = treenodes.iterator();
        while ( nodes_itr.hasNext() ) {
            node = ( TreeNode ) nodes_itr.next();
            if ( node.getEarliestTime() <= dobj.getEarliestTime() ) {
                node.add( dobj );
                last_dobj_added       = dobj;
                hasDrawableBeenStored = true;
                break;
            }
        }

        // Before the 1st leaf( 0, 0 ) is full, 
        // leaf and root point at the same node
        if ( ! hasDrawableBeenStored ) {
            root.add( dobj );
            last_dobj_added       = dobj;
            hasDrawableBeenStored = true;  // redundant & useless
            // In case dobj.getEarliestTime() is the earliest of all drawables
            root.affectEarliestTime( dobj.getEarliestTime() );
        }
    }

    private void switchToNextBranch()
    {
        TreeNode     node;
        TreeNodeID   nextBranchNodeID;
        double       prev_node_earliest_time;

        // starts from the leaf on next branch
        nextBranchNodeID = new TreeNodeID( leaf.getTreeNodeID() );
        nextBranchNodeID.toNextSibling();

        //  next()      goes from  leafs  to  root,
        //  previous()  goes from  root   to  leafs.
        Iterator nodes_itr = treenodes.iterator();
        while ( nodes_itr.hasNext() ) {
            node = ( TreeNode ) nodes_itr.next();
            if ( ! node.getTreeNodeID().equals( nextBranchNodeID ) ) {
                // Save the node onto the disk
                node.finalizeLatestTime( last_dobj_added );
                slog.writeTreeNode( node );

                // Empty the node for reuse.
                node.empty();

                // ReInitialize the node's TimeBoundingBox
                prev_node_earliest_time = node.getEarliestTime();
                node.setEarliestTime( node.getLatestTime() );

                // Reset the node with a new TreeNodeID
                node.setTreeNodeID( nextBranchNodeID );

                // set nextBranchNodeID to its Parent TreeNodeID
                nextBranchNodeID.toParent( num_leafs );

                // Grow new root if necessary
                /*  Grow new root if necessary
                 ( ! nodes_itr.hasNext() )
                 ==> ( nextBranchNodeID.isPossibleRoot() )

                 ( nextBranchNodeID.isPossibleRoot() )
                 !=> ( ! nodes_itr.hasNext() ) when num_children_per_node > 2
                */
                if ( ! nodes_itr.hasNext() ) {
                    root = new TreeNode( node );
                    root.setMapOfTopologyToShadowDef( shadowdefs_map );
                    root.setTreeNodeID( nextBranchNodeID );
                    root.affectEarliestTime( prev_node_earliest_time );
                    treenodes.add( root );
                    break;
                }
            }
            else //  node.getTreeNodeID == nextBranchNodeID
                break;
        }   //  Endof while ( nodes_itr.hasNext() )
    }

    // "true" is returned if last_dobj_added is not null before flushing
    // i.e. at least one drawable has been added the TreeTrunk.
    public boolean flushToFile()
    {
        TreeNode    node;
        Iterator    nodes_itr;

        if ( last_dobj_added == null ) {
            return false;
        }

        // System.err.println( "TreeTrunk.flushToFile(): START" );
        //  next()      goes from  leafs  to  root,
        nodes_itr = treenodes.iterator();
        while ( nodes_itr.hasNext() ) {
            node = ( TreeNode ) nodes_itr.next();
            node.finalizeLatestTime( last_dobj_added );
            slog.writeTreeNode( node );
            if ( ! nodes_itr.hasNext() ) { // node = root ?
                lineIDmapOne = node.getIdentityLineIDMap();
                node.summarizeCategories();
            }
            node.empty();  // empty the node for reuse.
        }

        last_dobj_added = null;
        // System.err.println( "TreeTrunk.flushToFile(): END" );
        return true;
    }

    public LineIDMap getIdentityLineIDMap()
    {
        return lineIDmapOne;
    }
}

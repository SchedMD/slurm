/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import javax.swing.JList;
import javax.swing.JScrollPane;
import javax.swing.event.TreeWillExpandListener;
import javax.swing.event.TreeExpansionListener;
import javax.swing.event.TreeModelListener;
import javax.swing.event.TreeExpansionEvent;
import javax.swing.event.TreeModelEvent;
import javax.swing.tree.ExpandVetoException;
import javax.swing.tree.TreePath;
import javax.swing.tree.TreeNode;
import javax.swing.tree.DefaultMutableTreeNode;
import java.util.List;
import java.util.Vector;
import java.util.Enumeration;

public class YaxisList extends JList
                       implements TreeWillExpandListener,
                                  TreeExpansionListener,
                                  TreeModelListener
{
    private YaxisTree      tree_view;
    private List           list_data;
    private JScrollPane    scroller;    // Enclosing JScrollPane of the JList

    public YaxisList( YaxisTree in_tree )
    {
        super();
        tree_view = in_tree;   
        list_data = new Vector();
        super.setListData( (Vector) list_data );
    }

    public void setEnclosingScroller( JScrollPane in_scroller )
    {
        scroller = in_scroller;
    }

    public void init()
    {
        TreePath  node_path;
        int       idx, row_count;

        list_data.clear();
        row_count = tree_view.getRowCount();
        for ( idx = 0; idx < row_count; idx++ ) {
            node_path = tree_view.getPathForRow( idx );
            list_data.add( tree_view.getNamedVtr( node_path ) );
        }
        super.setListData( (Vector) list_data );
        scroller.revalidate();
        scroller.repaint();
    }

    public void paintBlank( TreePath [] paths )
    {
        for ( int idx = 0; idx < paths.length; idx++ ) {
            list_data.set( tree_view.getRowForPath( paths[ idx ] ), "" );
            super.setListData( (Vector) list_data );
            scroller.revalidate();
            scroller.repaint();
        }
    }

    // TreeWillExpandListner
    public void treeWillCollapse( TreeExpansionEvent evt )
    throws ExpandVetoException
    {
        TreePath      node_path;
        TreeNode      node;
        Enumeration   paths;
        Object        child;
        int           row, childCount, idx;

        node_path   = evt.getPath();
        node        = (TreeNode) node_path.getLastPathComponent();
        childCount  = node.getChildCount();
        if ( Debug.isActive() )
            Debug.println( "treeWillCollapsed(): node_path = " + node_path );

        // Count all the descendant nodes( included expanded ones ) 
        //     JTree.getExpandedDescendants( node ) has to be called 
        //     before the node is collapsed, otherwise it returns null.
        paths = tree_view.getExpandedDescendants( node_path );
        while ( paths != null && paths.hasMoreElements() ) {
            child = ( (TreePath) paths.nextElement() ).getLastPathComponent();
            if ( Debug.isActive() )
                Debug.println( "treeWillCollapsed(): " + child + "'s level = "
                             + ( (DefaultMutableTreeNode) child ).getLevel() );
            // for some strange reason, getExpandedDescendants() returns
            // parent_path in the argument as one of the enumeration.  
            // Doing a check to avoid double counting.
            if ( ! child.equals( node ) )
                childCount += ( (TreeNode) child ).getChildCount();
            else
                if ( Debug.isActive() )
                    Debug.println( "treeWillCollapsed(): child = parent " );
        }

        row         = tree_view.getRowForPath( node_path );
        for ( idx = childCount; idx > 0 ; idx-- )
             list_data.remove( row + idx );  // inefficient removal
    }

    // TreeExpansionListner
    public void treeCollapsed( TreeExpansionEvent evt )
    {
        TreePath      node_path;

        node_path   = evt.getPath();

        // Update the collapsed node after tree is collapsed
        list_data.set( tree_view.getRowForPath( node_path ),
                       tree_view.getNamedVtr( node_path ) );

        super.setListData( (Vector) list_data );
        scroller.revalidate();
        scroller.repaint();
    }

    // TreeWillExpandListner
    public void treeWillExpand( TreeExpansionEvent evt )
    throws ExpandVetoException
    {
    }

    // TreeExpansionListner
    public void treeExpanded( TreeExpansionEvent evt )
    {
        TreePath      node_path;
        TreePath      child_path;
        Enumeration   children;
        Object        child;
        String        label;
        int           row_idx;

        node_path   = evt.getPath();
        children    = ( (TreeNode) node_path.getLastPathComponent() )
                      .children();

        // Update the expanded node first
        row_idx = tree_view.getRowForPath( node_path );
        if ( Debug.isActive() )
            Debug.println( "treeExpanded(): " + node_path
                         + " at row " + row_idx );
        if ( row_idx >= 0 && row_idx < list_data.size() )
            list_data.set( row_idx, tree_view.getNamedVtr( node_path ) );
        else
            list_data.add( tree_view.getNamedVtr( node_path ) );

        while ( children.hasMoreElements() ) {
            child = children.nextElement();
            child_path = node_path.pathByAddingChild( child );
            row_idx = tree_view.getRowForPath( child_path );
            if ( Debug.isActive() ) {
                Debug.println( "treeExpanded(): " + child_path + "'s level = " 
                             + ( (DefaultMutableTreeNode) child ).getLevel() );
                Debug.println( "treeExpanded(): " + child_path + " at row = "
                             + row_idx);
            }
            if ( row_idx >= 0 && row_idx < list_data.size() )
                list_data.add( row_idx, tree_view.getNamedVtr( child_path ) );
            else
                list_data.add( tree_view.getNamedVtr( child_path ) );
                                          // inefficient addition
            if ( tree_view.isExpanded( child_path ) )
                tree_view.fireTreeExpanded( child_path );
        }

        super.setListData( (Vector) list_data );
        scroller.revalidate();
        scroller.repaint();
    }

    // TreeModelListener
    public void treeNodesChanged( TreeModelEvent evt )
    {
        if ( Debug.isActive() )
            Debug.println( "treeNodesChanged(): evt = " + evt );
    }

    public void treeNodesInserted( TreeModelEvent evt )
    {
        TreePath      parent_path;
        Object []     children;
        int []        child_indices;
        TreePath      child_path;
        int           row_idx, idx;

        parent_path    = evt.getTreePath();
        children       = evt.getChildren();
        child_indices  = evt.getChildIndices(); 

        for ( idx = 0; idx < children.length; idx++ ) {
            child_path = parent_path.pathByAddingChild( children[ idx ] );
            row_idx = tree_view.getRowForPath( child_path );
            if ( Debug.isActive() )
                Debug.println( "treeNodesInserted(): " + child_path
                             + " at row " + row_idx );
            if ( row_idx >= 0 )
                list_data.add( row_idx, tree_view.getNamedVtr( child_path ) );
            else
                list_data.add( tree_view.getNamedVtr( child_path ) );
        }

        super.setListData( (Vector) list_data );
        scroller.revalidate();
        scroller.repaint();
    }

    public void treeNodesRemoved( TreeModelEvent evt )
    {
        TreePath      parent_path;
        Object []     children;
        named_vector  list_elem;
        int           parent_row, child_row, row_idx;

        parent_path    = evt.getTreePath();
        children       = evt.getChildren();

        parent_row = tree_view.getRowForPath( parent_path );
        if ( Debug.isActive() )
            Debug.println( "treeNodesRemoved(): parent " + parent_path
                         + " at row " + parent_row );
        child_row = parent_row + 1;
        for ( int idx = 0; idx < children.length; idx++ ) {
            while ( child_row < list_data.size() ) {
                list_elem = (named_vector) list_data.get( child_row );
                if ( list_elem.isNameEqualTo( children[ idx ].toString() ) ) {
                    list_data.remove( child_row );
                    if ( Debug.isActive() )
                        Debug.println( "treeNodesRemoved(): at row "
                                     + child_row );
                    break;
                }
                else
                    child_row++;
            }
        }

        super.setListData( (Vector) list_data );
        scroller.revalidate();
        scroller.repaint();
    }

    public void treeStructureChanged( TreeModelEvent evt )
    {
        if ( Debug.isActive() )
            Debug.println( "treeStructureChanged(): evt = " + evt );
    }
}

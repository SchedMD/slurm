/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import javax.swing.JTree;
import javax.swing.tree.DefaultMutableTreeNode;
import javax.swing.tree.TreePath;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Enumeration;

// import viewer.common.Routines;
// import viewer.common.Parameters;

public class YaxisTree extends JTree
{
    private DefaultMutableTreeNode  tree_root;
    private TreePath                root_path;

    private List[]                  leveled_paths;
    private int                     max_level;
    private int                     next_expanding_level;
 
    private List                    cut_paste_buf;
    private int                     buf_level;

    public YaxisTree( DefaultMutableTreeNode in_root )
    {
        super( in_root );
        tree_root = in_root;
        root_path = new TreePath( tree_root );
        super.putClientProperty("JTree.lineStyle", "Angled");
        // this.setRootVisible( false );
        // this.setEditable( true );
    }

    private void getAllLeavesForNode( named_vector nvtr,
                                      DefaultMutableTreeNode node )
    {
        DefaultMutableTreeNode child;
        Enumeration children  = node.children();
        while ( children.hasMoreElements() ) {
            child = (DefaultMutableTreeNode) children.nextElement();
            if ( child.isLeaf() )
                nvtr.add( child.getUserObject() );
            else
                getAllLeavesForNode( nvtr, child );
        }
    }

    public named_vector getNamedVtr( TreePath node_path )
    {
        DefaultMutableTreeNode node = (DefaultMutableTreeNode)
                                      node_path.getLastPathComponent();
        named_vector nvtr = new named_vector( node.toString() );
        if ( ! super.isExpanded( node_path ) )
            getAllLeavesForNode( nvtr, node );
        return nvtr;
    }

/*
    private void tree_grow_leaves( DefaultMutableTreeNode parent,
                                   int                    numLevels,
                                   int                    numNodes,
                                   int                    level_idx,
                                   int                    prev_node_idx )
    {
        DefaultMutableTreeNode child;
        String node_label;
        int idx_offset, node_idx;

        if ( level_idx < numLevels ) {
            idx_offset = prev_node_idx * numNodes;
            for ( int idx = 0; idx < numNodes; idx++ ) {
                node_idx = idx_offset + idx;
                node_label = "l=" + level_idx + " "
                           + "n=(" + prev_node_idx + ", " + node_idx + ")";
                child = new DefaultMutableTreeNode( node_label );
                tree_grow_leaves( child, numLevels, numNodes,
                                  level_idx + 1, node_idx );
                parent.add( child );
            }
        }
    }

    private void tree_construction( DefaultMutableTreeNode root )
    {
        int numLevels = 5;
        int numNodesPerParent  = 3;
        tree_grow_leaves( root, numLevels, numNodesPerParent, 1, 0 );
        ( (DefaultTreeModel) super.getModel() ).reload();
    }
*/

    public void init()
    {
        // this.tree_construction( tree_root );
        this.update_leveled_paths();
        // System.out.println( "YaxisTree.init(): VisibleRowCount = "
        //                   + super.getVisibleRowCount() + ", VisibleFrame = "
        //                   + super.getVisibleRowCount()
        //                   * super.getRowHeight() );
        super.setEditable( true );

        // this.initDisplaySize();
    }

/*
    private void initDisplaySize()
    {
        int avail_screen_height;
        int canvas_height;
        int y_tree_row_height;
        int y_tree_row_count;
        int vis_row_count;

        super.setRootVisible( Parameters.Y_AXIS_ROOT_VISIBLE );
        y_tree_row_count    = super.getRowCount();
        avail_screen_height = (int) ( Routines.getScreenSize().height
                                    * Parameters.SCREEN_HEIGHT_RATIO );
        y_tree_row_height   = avail_screen_height / y_tree_row_count;
        super.setRowHeight( y_tree_row_height );
        super.setVisibleRowCount( y_tree_row_count );
        System.out.println( "avail_screen_height = " + avail_screen_height +"\n"
                          + "y_tree_row_height = " + y_tree_row_height + "\n"
                          + "y_tree_row_count = " + y_tree_row_count + "\n"
                          + "canvas_height = " + canvas_height + "\n"
                          + "vis_row_count = " + vis_row_count );
    }
*/

    public void update_leveled_paths()
    {
        Iterator                paths;
        Enumeration             nodes;
        DefaultMutableTreeNode  node;
        TreePath                path;
        int                     ilevel;

        // Update the tree's Maximum allowed Level
        max_level = tree_root.getLastLeaf().getLevel();

        if ( Debug.isActive() ) {
            Debug.println( "tree_root(" + tree_root + ").level="
                         + tree_root.getLevel() );
            Debug.println( "last_leaf(" + tree_root.getLastLeaf() + ").level="
                         + max_level );
        }

        // Initialize the leveled_paths[] sizes
        leveled_paths = new ArrayList[ max_level + 1 ];
        leveled_paths[ 0 ] = new ArrayList( 1 );
        for ( ilevel = 1; ilevel <= max_level; ilevel++ )
            leveled_paths[ ilevel ] = new ArrayList();

        // Update the leveled_paths[]'s content
        nodes = tree_root.breadthFirstEnumeration();
        if ( nodes != null )
            while ( nodes.hasMoreElements() ) {
                node = (DefaultMutableTreeNode) nodes.nextElement();
                path = new TreePath( node.getPath() );
                leveled_paths[ node.getLevel() ].add( path );
            }

         /*
         for ( ilevel = 0; ilevel <= max_level; ilevel++ ) {
             paths = leveled_paths[ ilevel ].iterator();
             while ( paths.hasNext() )
                 System.out.println( paths.next() );
             System.out.println();
         }
         */

         // Update next_expanding_level
         boolean isAllExpanded = true;
         ilevel = 0;
         next_expanding_level = ilevel;
         while ( ilevel < max_level && isAllExpanded ) {
             paths = leveled_paths[ ilevel ].iterator();
             while ( paths.hasNext() && isAllExpanded ) {
                 path = (TreePath) paths.next();
                 isAllExpanded = isAllExpanded && super.isExpanded( path );
             }
             ilevel++;
         }
         if ( ilevel > max_level )
             next_expanding_level = max_level;
         else
             next_expanding_level = ilevel - 1;
    }

    public boolean isLevelExpandable()
    {
        return next_expanding_level < max_level;
    }

    public void expandLevel()
    {
        Iterator    paths;
        TreePath    path;

        if ( ! isLevelExpandable() )
            return;

        paths = leveled_paths[ next_expanding_level ].iterator();
        while ( paths.hasNext() ) {
            path = (TreePath) paths.next();
            if ( super.isCollapsed( path ) )
                super.expandPath( path );
        }
        if ( next_expanding_level < max_level )
            next_expanding_level++;
        else
            next_expanding_level = max_level;
    }

    public boolean isLevelCollapsable()
    {
        int next_collapsing_level = next_expanding_level - 1;
        return next_collapsing_level >= 0;
    }

    public void collapseLevel()
    {
        Iterator    paths;
        TreePath    path;
        int         next_collapsing_level;

        if ( ! isLevelCollapsable() )
            return;

        next_collapsing_level = next_expanding_level - 1;
        paths = leveled_paths[ next_collapsing_level ].iterator();
        while ( paths.hasNext() ) {
            path = (TreePath) paths.next();
            if ( super.isExpanded( path ) )
                super.collapsePath( path );
        }
        next_expanding_level = next_collapsing_level;
    }

    //  Manipulation of the Cut&Paste buffer
    public void renewCutAndPasteBuffer()
    {
        buf_level = -1;
        if ( cut_paste_buf != null )
            cut_paste_buf.clear();
        else
            cut_paste_buf = new ArrayList();
    }

    public boolean isPathLevelSameAsThatOfCutAndPasteBuffer( TreePath path )
    {
        return buf_level == this.getLastPathComponentLevel( path );
    }

    private int getLastPathComponentLevel( TreePath path )
    {
        DefaultMutableTreeNode node;
        node = (DefaultMutableTreeNode) path.getLastPathComponent();
        return node.getLevel();
    }

    public boolean isCutAndPasteBufferUniformlyLeveled( TreePath [] paths )
    {
        if ( paths != null && paths.length > 0 ) {
            int ilevel = this.getLastPathComponentLevel( paths[ 0 ] );
            for ( int idx = 1; idx < paths.length; idx++ ) {
                if ( ilevel != this.getLastPathComponentLevel( paths[ idx ] ) )
                    return false;
            }
            buf_level = ilevel;
        }
        return true;
    }

    public void addToCutAndPasteBuffer( TreePath [] paths )
    {
        if ( cut_paste_buf != null ) {
            for ( int idx = 0; idx < paths.length; idx++ )
                cut_paste_buf.add( paths[ idx ] );
        }
    }

    public int getLevelOfCutAndPasteBuffer()
    {
        return buf_level;
    }

    public TreePath[] getFromCutAndPasteBuffer()
    {
        if ( cut_paste_buf != null ) {
            Object [] objs    = cut_paste_buf.toArray();
            TreePath [] paths = new TreePath[ objs.length ];
            for ( int idx = 0; idx < objs.length; idx++ )
                paths[ idx ] = (TreePath) objs[ idx ];
            return paths;
        }
        else
            return null;
    }

    public void clearCutAndPasteBuffer()
    {
        if ( cut_paste_buf != null )
            cut_paste_buf.clear();
    }
}

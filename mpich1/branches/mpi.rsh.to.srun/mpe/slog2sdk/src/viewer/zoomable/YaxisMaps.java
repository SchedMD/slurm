/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import javax.swing.tree.TreeNode;
import javax.swing.tree.TreePath;

import java.util.Set;
import java.util.TreeSet;
import java.util.Map;
import java.util.HashMap;
import java.util.TreeMap;
import java.util.Iterator;
import java.util.Enumeration;
import java.io.PrintStream;

import logformat.slog2.LineIDMap;
import viewer.common.Parameters;

/*
 *   This class provides various Mapping needed to draw the CanvasXXXXline
 *   The forward mapping is LineID to RowID, i.e. Map( LineID -> RowID ),
 *   where LineID is the Yaxis LineID in drawable.Coord of Drawable.
 *   and   RowID  is the Row Number of the CanvasXXXXline corresponding to JTree
 *
 *   The SLOG2 file provides LineIDMap, i.e. Map( LineID -> UserObj[] ),
 *   where UserObj[] contains actual hierarchical user objects, like
 *   nodeID, local-processID, local-threadID.....  Currently, UserObj[]
 *   is of type Integer[], but it could be String[]( Untested assumption! ).
 *   This is a many-to-one mapping.
 *
 *   First convert LineIDMap to a Map( LineID -> YaxisTreeLeaf ).
 *   YaxisTreeLeaf allows tracking of the movement of YaxisTreeNode(s).
 *   It provides a unique idenification of YaxisTreeNode(s) in Display. 
 *   The Map is the "state" of LineIDs.  Again, this is a many-to-one mapping.
 *
 *   JTree provides Map( TreePath -> RowID ) which is also a many-to-one
 *   mapping.  Map( LineID -> YaxisTreeLeaf ) + Map( TreePath -> RowID )
 *   ==> Map( LineID -> RowID ).  It is a many-to-one mapping as well.
 */
public class YaxisMaps
{
    private LineIDMap           lineIDmap;
    private YaxisTreeNode       y_treeroot;
    private YaxisTree           tree_view;

    private Map                 map_line2treeleaf;
    private Map                 map_treepath2row;
    private Map                 map_line2row;

    public YaxisMaps( final LineIDMap  in_linemap )
    {
        lineIDmap          = in_linemap;
        y_treeroot         = null;
        tree_view          = null;
        map_line2treeleaf  = null;
        map_treepath2row   = null;
        map_line2row       = null;
    }

    public LineIDMap getLineIDMap()
    {
        return lineIDmap;
    }

    // getTreeRoot() is part of the initialization procedure of YaxisMaps
    public YaxisTreeNode getTreeRoot()
    {
        if ( y_treeroot == null ) {
            // y_treeroot  = new YaxisTreeNode( lineIDmap.getTitle() );
            y_treeroot  = new YaxisTreeNode( Parameters.Y_AXIS_ROOT_LABEL );
            this.convertLineIDMapToTree();
        }
        return y_treeroot;
    }

    // setTreeView() is part of the initialization procedure of YaxisMaps
    public void setTreeView( YaxisTree  in_tree )
    {
        tree_view  = in_tree;
        if ( map_line2treeleaf == null ) {
            // map_line2treeleaf  = new HashMap();
            map_line2treeleaf  = new TreeMap();
            this.setMapOfLineIDToTreeLeaf();
        }
    }

    public YaxisTree getTreeView()
    {
        return tree_view;
    }

    public Map getMapOfLineIDToTreeLeaf()
    {
        return map_line2treeleaf;
    }

    public Map getMapOfTreePathToRowID()
    {
        return map_treepath2row;
    }

    public Map getMapOfLineIDToRowID()
    {
        return map_line2row;
    }

    //  To be invoked by ActionYaxisTreeCommit()
    public boolean update()
    {
        this.updateMapOfTreePathToRowID();
        return updateMapOfLineIDToRowID();
    }

    private void convertLineIDMapToTree()
    {
        Set            set_userpath;
        YaxisTreeNode  parent, child;
        Object         user_obj;
        Integer[]      iary;
        int            idx;

        // Use TreeSet with IntegerArrayComparator to initialize the JTree.
        // The init state of JTree's nodes are arranged in certain order
        set_userpath   = new TreeSet( new IntegerArrayComparator() );
        set_userpath.addAll( lineIDmap.values() );
        Iterator iarys = set_userpath.iterator();
        while ( iarys.hasNext() ) {
            parent = y_treeroot;
            iary = (Integer[]) iarys.next();
            for ( idx = 0; idx < iary.length; idx++ ) {
                 user_obj = iary[ idx ];
                 child = parent.getChild( user_obj );
                 if ( child == null ) {
                     child = new YaxisTreeNode( user_obj );
                     parent.add( child );
                 }
                 parent = child;
            }
        }
        // System.out.println( this.stringForSetOfUserObjs() );
    }

    private boolean setMapOfLineIDToTreeLeaf()
    {
        YaxisTreeNode  node, child;
        Map.Entry      entry;
        Object         user_obj;
        Integer[]      iary;
        boolean        isOK;
        int            idx;

        isOK  = true;
        Iterator entries = lineIDmap.entrySet().iterator();
        while ( entries.hasNext() ) {
            node        = y_treeroot;
            entry       = (Map.Entry) entries.next();
            iary        = (Integer[]) entry.getValue();
            for ( idx = 0; idx < iary.length; idx++ ) {
                 user_obj = iary[ idx ];
                 child = node.getChild( user_obj );
                 if ( child == null ) {
                     child = new YaxisTreeNode( user_obj );
                     node.add( child );
                     System.err.println( "YaxisMaps."
                                       + "setMapOfLineIDToTreeLeaf(): "
                                       + "Unexpected Error!\n"
                                       + "\t user object " + user_obj
                                       + " is NOT found under the root! " );
                     isOK = false;
                 }
                 node       = child;
            }
            map_line2treeleaf.put( entry.getKey(), node );
        }
        // System.out.println( this.stringForMapOfLineIDToTreeLeaf() );
        return isOK;
    }

    private void updateMapOfTreePathToRowID()
    {
        TreePath   path;
        int        idx, row_count;

        if ( map_treepath2row == null )
            map_treepath2row = new HashMap();
        else
            map_treepath2row.clear();

        row_count = tree_view.getRowCount();
        for ( idx = 0; idx < row_count; idx++ ) {
            path = tree_view.getPathForRow( idx );
            map_treepath2row.put( path, new Integer( idx ) );
        }
        // System.out.println( this.stringForMapOfTreePathToRowID() );
    }

    private boolean updateMapOfLineIDToRowID()
    {
        Map.Entry      entry;
        YaxisTreeNode  node;
        TreePath       path;
        Integer        lineID, rowID;
        boolean        isOK;

        if ( map_line2row == null )
            // map_line2row = new HashMap();
            map_line2row = new TreeMap();
        else
            map_line2row.clear();

        isOK  = true;
        Iterator entries = map_line2treeleaf.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry   = (Map.Entry)      entries.next();
            lineID  = (Integer)        entry.getKey();
            node    = (YaxisTreeNode)  entry.getValue();
            path    = new TreePath( node.getPath() );
            rowID   = null;
            while ( rowID == null && path != null ) {
                rowID   = (Integer) map_treepath2row.get( path );
                path    = path.getParentPath();
            }
            if ( rowID == null )
                isOK = false;
            map_line2row.put( lineID, rowID );
        }
        // System.out.println( this.stringForMapOfLineIDToRowID() );
        return isOK;
    }

    private static String stringForSetOfUserObjs( final TreeSet set_userpath )
    {
        Integer[]  iary;
        int        idx;

        StringBuffer rep = new StringBuffer( "SetOfUserObjects:\n" );
        Iterator iarys = set_userpath.iterator();
        while ( iarys.hasNext() ) {
            iary = (Integer[]) iarys.next();
            for ( idx = 0; idx < iary.length; idx++ )
                rep.append( iary[ idx ] + " " );
            rep.append( "\n" );
        }
        return rep.toString();
    }

    public static String stringFromYaxisRoot( final YaxisTreeNode  treeroot )
    {
        YaxisTreeNode  node;

        StringBuffer  rep   = new StringBuffer();
        int           level = treeroot.getLevel();
        Enumeration nodes = treeroot.breadthFirstEnumeration();
        while ( nodes.hasMoreElements() ) {
            node = (YaxisTreeNode) nodes.nextElement();
            if ( level != node.getLevel() ) {
                level = node.getLevel();
                rep.append( "\n" );
            }
            rep.append( " " + node );
        }
        return rep.toString();
    }

    public String stringForYaxisTree()
    {
        return "YaxisTree:\n" + stringFromYaxisRoot( y_treeroot );
    }

    public static String stringForMap( final Map  map )
    {
        Map.Entry  entry;

        StringBuffer rep = new StringBuffer();
        Iterator entries = map.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry  = (Map.Entry) entries.next();
            rep.append( " [ " + entry.getKey() + " ] => "
                      + entry.getValue() + "\n" );
        }
        return rep.toString();
    }

    public String stringForMapOfLineIDToTreeLeaf()
    {
        Map.Entry   entry;
        TreeNode[]  nodes;
        int         idx, last_idx;

        StringBuffer rep = new StringBuffer( "MapOfLineIDToTreeLeaf:\n" );
        Iterator entries = map_line2treeleaf.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry  = (Map.Entry) entries.next();
            rep.append( " [ " + entry.getKey() + " ] => { " );
            nodes = ( (YaxisTreeNode) entry.getValue() ).getPath();
            last_idx = nodes.length-1;
            for ( idx = 0; idx < last_idx; idx++ )
                rep.append( nodes[ idx ] + " " );
            rep.append( "(" + nodes[ last_idx ] + ") }\n" );
        }
        return rep.toString();
    }

    public String stringForMapOfTreePathToRowID()
    {
        return "MapOfTreePathToRowID:\n" + stringForMap( map_treepath2row );
    }

    public String stringForMapOfLineIDToRowID()
    {
        return "MapOfLineIDToRowID:\n" + stringForMap( map_line2row );
    }

    public void printMaps( PrintStream  pstm )
    {
        pstm.println( stringForMapOfLineIDToTreeLeaf() );
        pstm.println( stringForMapOfTreePathToRowID() );
        pstm.println( stringForMapOfLineIDToRowID() );
    }
}

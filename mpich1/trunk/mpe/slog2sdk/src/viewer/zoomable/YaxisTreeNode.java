/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.Enumeration;
import javax.swing.tree.DefaultMutableTreeNode;

public class YaxisTreeNode extends DefaultMutableTreeNode
{
    public YaxisTreeNode( Object user_obj )
    {
        super( user_obj );
    }

    /*
    public int getIndex( final Object user_obj )
    {
        DefaultMutableTreeNode node;
        Enumeration nodes = super.children();
        while ( nodes.hasMoreElements() ) {
            node = (DefaultMutableTreeNode) nodes.nextElement();
            if ( node.getUserObject().equals( user_obj ) )
                return super.getIndex( node );
        }
        return -1;
    }
    */

    public YaxisTreeNode getChild( final Object user_obj )
    {
        YaxisTreeNode node;
        Enumeration nodes = super.children();
        while ( nodes.hasMoreElements() ) {
            node = (YaxisTreeNode) nodes.nextElement();
            if ( node.getUserObject().equals( user_obj ) )
                return node;
        }
        return null;
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.*;
import java.awt.event.*;
import java.net.*;
import javax.swing.*;
import javax.swing.tree.*;

import viewer.common.Dialogs;

public class ActionTimelineMark implements ActionListener
{
    private Window             root_window;
    private ToolBarStatus      toolbar;
    private YaxisTree          tree;
    private DefaultTreeModel   tree_model;

    public ActionTimelineMark( Window           parent_window,
                               ToolBarStatus    in_toolbar,
                               YaxisTree        in_tree )
    {
        root_window  = parent_window;
        toolbar      = in_toolbar;
        tree         = in_tree;
        tree_model   = (DefaultTreeModel) tree.getModel();
    }

    public void actionPerformed( ActionEvent event )
    {
        if ( Debug.isActive() )
            Debug.println( "Action for Mark Timeline button" );

        TreePath[]        selected_paths;
        MutableTreeNode   node;

        if ( tree.getSelectionCount() < 1 ) {
            Dialogs.error( root_window,
                           "At least ONE tree node needs to be marked!" );
            return;
        }

        selected_paths = tree.getSelectionPaths();
        for ( int idx = 0; idx < selected_paths.length; idx++ ) {
            node = (MutableTreeNode) selected_paths[idx].getLastPathComponent();
            if ( Debug.isActive() ) {
                if ( tree.isExpanded( selected_paths[ idx ] ) )
                    Debug.println( "\tselected an expanded node " + node );
                else
                    Debug.println( "\tselected a collapsed node " + node );
            }
        }
        tree.renewCutAndPasteBuffer();
        if ( tree.isCutAndPasteBufferUniformlyLeveled( selected_paths ) )
            tree.addToCutAndPasteBuffer( selected_paths );
        else {
            Dialogs.error( root_window,
                           "The tree nodes are NOT selected from the "
                         + "same level!  Select again." );
            return;
        }

        // Set toolbar buttons to reflect status
        toolbar.getTimelineMarkButton().setEnabled( true );
        toolbar.getTimelineMoveButton().setEnabled( true );
        toolbar.getTimelineDeleteButton().setEnabled( true );
    }
}

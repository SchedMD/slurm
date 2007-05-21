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

public class ActionTimelineDelete implements ActionListener
{
    private Window             root_window;
    private ToolBarStatus      toolbar;
    private YaxisTree          tree;
    private DefaultTreeModel   tree_model;

    public ActionTimelineDelete( Window           parent_window,
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
            Debug.println( "Action for Delete Timeline button" );

        TreePath[]        child_paths;
        MutableTreeNode   child;
        int               idx;
        
        if ( ! Dialogs.confirm( root_window,
               "Are you sure to PERMANENTLY delete the marked items?" ) )
            return;

        child_paths    = tree.getFromCutAndPasteBuffer();

        if ( child_paths.length < 1 ) {
            Dialogs.warn( root_window,
                          "Nothing has been marked for removal!" );
            return;
        }

        // Collapse any marked timelines which are expanded
        for ( idx = 0; idx < child_paths.length; idx++ ) {
            if ( tree.isExpanded( child_paths[ idx ] ) ) {
                if ( Debug.isActive() )
                    Debug.println( "\tCollapse " + child_paths[ idx ] );
                tree.collapsePath( child_paths[ idx ] );
            }
        }

        // Remove the marked timelines from the tree
        for ( idx = 0; idx < child_paths.length; idx++ ) {
            child = (MutableTreeNode) child_paths[ idx ].getLastPathComponent();
            tree_model.removeNodeFromParent( child );
            if ( Debug.isActive() )
                Debug.println( "\tCut " + child );
        }

        // Clear up the marked timelines in the buffer
        tree.clearCutAndPasteBuffer(); 

        // Update leveled_paths[]
        tree.update_leveled_paths();

        // Set toolbar buttons to reflect status
        toolbar.getTimelineMarkButton().setEnabled( true );
        toolbar.getTimelineMoveButton().setEnabled( false );
        toolbar.getTimelineDeleteButton().setEnabled( false );
        toolbar.resetYaxisTreeButtons();
    }
}

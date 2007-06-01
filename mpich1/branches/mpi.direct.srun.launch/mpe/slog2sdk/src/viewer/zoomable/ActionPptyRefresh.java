/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.JButton;

import viewer.common.TopWindow;
import viewer.common.Parameters;
import viewer.common.PreferenceFrame;

public class ActionPptyRefresh implements ActionListener
{
    private YaxisTree        y_tree;
    private JButton          y_tree_redraw_btn;

    private PreferenceFrame  pptys_frame;

    public ActionPptyRefresh( YaxisTree in_y_tree, JButton toolbar_commit_btn )
    {
        y_tree             = in_y_tree;
        y_tree_redraw_btn  = toolbar_commit_btn;
    }

    public void actionPerformed( ActionEvent event )
    {
        if ( Debug.isActive() )
            Debug.println( "Action for Refresh button" );

        pptys_frame = (PreferenceFrame) TopWindow.Preference.getWindow();
        if ( pptys_frame != null )
            pptys_frame.updateAllParametersFromFields();
        Parameters.initStaticClasses();
        y_tree.setRootVisible( Parameters.Y_AXIS_ROOT_VISIBLE );
        y_tree_redraw_btn.doClick();
    }
}

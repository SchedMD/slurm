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

import viewer.common.Dialogs;

public class ActionYaxisTreeCommit implements ActionListener
{
    private Window             root_window;
    private ToolBarStatus      toolbar;
    private ViewportTimeYaxis  canvas_vport;
    private YaxisMaps          y_maps;
    private RowAdjustments     row_adjs;

    public ActionYaxisTreeCommit( Window             parent_window,
                                  ToolBarStatus      in_toolbar,
                                  ViewportTimeYaxis  in_canvas_vport,
                                  YaxisMaps          in_maps,
                                  RowAdjustments     in_row_adjs )
    {
        root_window   = parent_window;
        toolbar       = in_toolbar;
        canvas_vport  = in_canvas_vport;
        y_maps        = in_maps;
        row_adjs      = in_row_adjs;
    }

    public void actionPerformed( ActionEvent event )
    {
        if ( Debug.isActive() )
            Debug.println( "Action for Commit YaxisTree button, Redraw!" );

        if ( ! y_maps.update() )
            Dialogs.error( root_window,
                           "Error in updating YaxisMaps!" );
        // y_maps.printMaps( System.out );
        canvas_vport.fireComponentResized();
        row_adjs.updateSlidersAfterTreeExpansion();

        /*
           There are too many occasion that need to redraw timelines canvas.
           Leave commit_btn enabled all the time. 1/18/2002
           Set toolbar buttons to reflect status
        */
        // toolbar.getYaxisTreeCommitButton().setEnabled( false );
    }
}

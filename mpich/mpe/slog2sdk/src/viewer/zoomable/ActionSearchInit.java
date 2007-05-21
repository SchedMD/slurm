/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.event.*;

public class ActionSearchInit implements ActionListener
{
    private ToolBarStatus      toolbar;
    private ViewportTimeYaxis  canvas_vport;

    public ActionSearchInit( ToolBarStatus      in_toolbar,
                             ViewportTimeYaxis  in_vport )
    {
        toolbar       = in_toolbar;
        canvas_vport  = in_vport;
    }

    public void actionPerformed( ActionEvent event )
    {
        canvas_vport.searchInit();

        if ( Debug.isActive() )
            Debug.println( "Action for Search Initialize button. " );
    }
}

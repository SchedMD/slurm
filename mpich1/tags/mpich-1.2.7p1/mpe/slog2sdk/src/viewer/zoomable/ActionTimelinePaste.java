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

public class ActionTimelinePaste implements ActionListener
{
    private TimelinePanel  top_panel;

    public ActionTimelinePaste( TimelinePanel panel )
    {
        top_panel = panel;
    }

    public void actionPerformed( ActionEvent event )
    {
        if ( Debug.isActive() )
            Debug.println( "Action for Paste Timeline button" );
    }
}

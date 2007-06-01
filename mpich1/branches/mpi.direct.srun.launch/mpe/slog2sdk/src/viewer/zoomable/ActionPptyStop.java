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

public class ActionPptyStop implements ActionListener
{
    private Window  top_window;

    public ActionPptyStop( final Window window )
    {
        top_window  = window;
    }

    public void actionPerformed( ActionEvent event )
    {
        if ( Debug.isActive() )
            Debug.println( "Action for Stop Property button" );
        top_window.dispose();
    }
}

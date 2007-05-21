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

public class ActionVportDown implements ActionListener
{
    private JScrollBar   scrollbar;

    public ActionVportDown( JScrollBar sb )
    {
        scrollbar = sb;
    }

    public void actionPerformed( ActionEvent event )
    {
        scrollbar.setValue( scrollbar.getValue()
                          + scrollbar.getHeight() / 2 );
        if ( Debug.isActive() )
            Debug.println( "Action for Down Viewport button" );
    }
}

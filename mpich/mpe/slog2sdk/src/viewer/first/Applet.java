/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.first;

import java.awt.*;
import java.awt.event.*;
import java.net.*;
import java.applet.*;
import javax.swing.*;

public class Applet extends JApplet
{
    public void init()
    {
        MainFrame.checkVersion();

        // Debug.setActive( true ); Debug.initTextArea();
    }

    public void start()
    {
        setContentPane( new MainPanel( this, null, 0 ) );
    }
}

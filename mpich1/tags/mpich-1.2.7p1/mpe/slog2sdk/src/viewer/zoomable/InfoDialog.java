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
import javax.swing.*;

import viewer.common.Dialogs;

public class InfoDialog extends JDialog
{
    private JPanel   btn_panel;
    private JButton  close_btn;
    private double   clicked_time;

    public InfoDialog( final Frame   ancestor_frame,
                             String  title_str,
                             double  time )
    {
        super( ancestor_frame, title_str );
        clicked_time = time;
        this.init();
    }

    public InfoDialog( final Dialog  ancestor_dialog,
                             String  title_str,
                             double  time )
    {
        super( ancestor_dialog, title_str );
        clicked_time = time;
        this.init();
    }

    private void init()
    {
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );
        btn_panel = new JPanel();
        close_btn = new JButton( "close" );
        close_btn.setAlignmentX( Component.CENTER_ALIGNMENT );
        btn_panel.add( close_btn );
        btn_panel.setAlignmentX( Component.LEFT_ALIGNMENT );
        Dimension  panel_max_size;
        panel_max_size        = btn_panel.getPreferredSize();
        panel_max_size.width  = Short.MAX_VALUE;
        btn_panel.setMaximumSize( panel_max_size );

        // addWindowListener( new InfoDialogWindowListener( this ) );
    }

    public JButton getCloseButton()
    {
        return close_btn;
    }

    public JPanel getCloseButtonPanel()
    {
        return btn_panel;
    }

    public double getClickedTime()
    {
        return clicked_time;
    }

    public void setVisibleAtLocation( final Point global_pt )
    {
        this.setLocation( global_pt );
        this.pack();
        this.setVisible( true );
        this.toFront();
    }

    private class InfoDialogWindowListener extends WindowAdapter
    {
        private InfoDialog  info_popup;

        public InfoDialogWindowListener( InfoDialog info )
        {
            info_popup = info;
        }

        public void windowClosing( WindowEvent e )
        {
            // info_popup.dispose();
            Dialogs.info( info_popup, "Use the CLOSE button please!", null );
        }
    }
}

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

import base.drawable.Drawable;
import viewer.common.TopWindow;

public class SearchDialog extends JDialog 
                          implements ActionListener
{
    private Window             root_window;
    private ViewportTimeYaxis  viewport;

    private Container          root_panel;
    private JPanel             btn_panel;
    private JButton            close_btn;

    public SearchDialog( final Frame frame, ViewportTimeYaxis  vport )
    {
        super( frame, "Search Box" );
        root_window  = frame;
        viewport     = vport;
        this.init();
    }

/*
    public SearchDialog( final Dialog dialog, ViewportTimeYaxis  vport )
    {
        super( frame, "Search Box" );
        root_window  = dialog;
        viewport     = vport;
        this.init();
    }
*/

    private void init()
    {
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );
        root_panel = super.getContentPane();
        root_panel.setLayout( new BoxLayout( root_panel, BoxLayout.Y_AXIS ) );

        btn_panel = new JPanel();
        close_btn = new JButton( "close" );
        close_btn.addActionListener( this );
        close_btn.setAlignmentX( Component.CENTER_ALIGNMENT );
        btn_panel.add( close_btn );
        btn_panel.setAlignmentX( Component.LEFT_ALIGNMENT );
        Dimension  panel_max_size;
        panel_max_size        = btn_panel.getPreferredSize();
        panel_max_size.width  = Short.MAX_VALUE;
        btn_panel.setMaximumSize( panel_max_size );

        super.addWindowListener( new WindowAdapter()
        {
            public void windowClosing( WindowEvent evt )
            {
                SearchDialog.this.setVisible( false );
                viewport.eraseSearchedDrawable();
                viewport.repaint();
            }
        } );

        super.setVisible( false );
    }

    private void setVisibleAtLocation( final Point global_pt )
    {
        this.setLocation( global_pt );
        this.pack();
        this.setVisible( true );
        this.toFront();
    }

    public void setVisibleAtDefaultLocation()
    {
        Rectangle rect   = null;
        Point     loc_pt = null;
        Frame     frame  = TopWindow.First.getWindow();
        if ( frame != null ) {
            rect    = frame.getBounds();
            loc_pt  = new Point( rect.x + rect.width, rect.y );
        }
        else {
            rect    = root_window.getBounds();
            loc_pt  = new Point( rect.x + rect.width, rect.y );
        }
        this.setVisibleAtLocation( loc_pt );
    }

    public void replace( Component cmpo_panel )
    {
        root_panel.removeAll();
        root_panel.add( cmpo_panel );
        root_panel.add( btn_panel );
        super.invalidate();
        super.validate();
    }

    public void actionPerformed( ActionEvent evt )
    {
        if ( evt.getSource() == close_btn ) {
            super.setVisible( false );
            viewport.eraseSearchedDrawable();
            viewport.repaint();
        }
    }

}

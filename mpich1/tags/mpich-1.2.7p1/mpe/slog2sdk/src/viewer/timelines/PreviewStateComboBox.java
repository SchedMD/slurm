/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.JComboBox;
import javax.swing.JButton;

import base.topology.PreviewState;
import viewer.common.Const;
import viewer.common.Parameters;
import viewer.common.TopWindow;
import viewer.common.PreferenceFrame;

public class PreviewStateComboBox extends JComboBox
{
    private JButton          canvas_redraw_btn;
    private PreferenceFrame  pref_frame;

    public PreviewStateComboBox()
    {
        super();
        super.setFont( Const.FONT );
        super.setEditable( false );
        super.addItem( PreviewState.FIT_MOST_LEGENDS );
        super.addItem( PreviewState.OVERLAP_INCLUSION );
        super.addItem( PreviewState.CUMULATIVE_INCLUSION );
        super.addItem( PreviewState.OVERLAP_EXCLUSION );
        super.addItem( PreviewState.CUMULATIVE_EXCLUSION );
        super.addItem( PreviewState.CUMULATIVE_EXCLUSION_BASE );
        super.setToolTipText( "Display options for the Preview state." );
        canvas_redraw_btn  = null;
        pref_frame         = null;
    }

    public void addRedrawListener( JButton btn )
    {
        canvas_redraw_btn = btn;
        super.addActionListener( new PreviewModeActionListener() );
        pref_frame = (PreferenceFrame) TopWindow.Preference.getWindow();
    }

    public void init()
    {
        /*
            Since JComboBox.setSelectedItem() invokes ActionListener which
            call canvas_redraw_btn.doClick(), i.e. JComboBox.setSelectedItem()
            redraws the Timeline window.  There init() needs to be called
            after RowAdjustment.initSlidersAndTextFields().
        */
        super.setSelectedItem( Parameters.PREVIEW_STATE_DISPLAY );
    }

    private class PreviewModeActionListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String display_str;
            display_str = (String) PreviewStateComboBox.this.getSelectedItem();
            // PreviewState.setDisplayType( display_str );
            Parameters.PREVIEW_STATE_DISPLAY = display_str;
            pref_frame.updateAllFieldsFromParameters();
            canvas_redraw_btn.doClick();
        }
    }
}

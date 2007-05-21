/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.histogram;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.JComboBox;
import javax.swing.JButton;

import base.topology.PreviewState;
import base.topology.SummaryState;
import viewer.common.Const;
import viewer.common.Parameters;
// import viewer.common.TopWindow;
// import viewer.common.PreferenceFrame;

public class SummaryStateComboBox extends JComboBox
{
    private JButton          canvas_redraw_btn;
    // private PreferenceFrame  pref_frame;

    public SummaryStateComboBox()
    {
        super();
        super.setFont( Const.FONT );
        super.setEditable( false );
        super.addItem( SummaryState.FIT_MOST_LEGENDS );
        super.addItem( SummaryState.OVERLAP_INCLUSION );
        super.addItem( SummaryState.OVERLAP_EXCLUSION );
        super.addItem( SummaryState.CUMULATIVE_EXCLUSION );
        super.setToolTipText( "Display options for the Summary state." );
        canvas_redraw_btn  = null;
        // pref_frame         = null;
    }

    public void addRedrawListener( JButton btn )
    {
        canvas_redraw_btn = btn;
        super.addActionListener( new SummaryModeActionListener() );
        // pref_frame = (PreferenceFrame) TopWindow.Preference.getWindow();
    }

    public void init()
    {
        /*
            Since JComboBox.setSelectedItem() invokes ActionListener which
            call canvas_redraw_btn.doClick(), i.e. JComboBox.setSelectedItem()
            redraws the Statline window.  There init() needs to be called
            after RowAdjustment.initSlidersAndTextFields().
        */
        if ( Parameters.PREVIEW_STATE_DISPLAY.equals(
             PreviewState.CUMULATIVE_INCLUSION ) )
            super.setSelectedItem( SummaryState.OVERLAP_INCLUSION );
        else if ( Parameters.PREVIEW_STATE_DISPLAY.equals(
                  PreviewState.CUMULATIVE_EXCLUSION_BASE ) )
            super.setSelectedItem( SummaryState.CUMULATIVE_EXCLUSION );
        else
            super.setSelectedItem( Parameters.PREVIEW_STATE_DISPLAY );
    }

    private class SummaryModeActionListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String display_str;
            display_str = (String) SummaryStateComboBox.this.getSelectedItem();
            SummaryState.setDisplayType( display_str );
            // Parameters.PREVIEW_STATE_DISPLAY = display_str;
            // pref_frame.updateAllFieldsFromParameters();
            canvas_redraw_btn.doClick();
        }
    }
}

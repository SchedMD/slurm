/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.Font;
import java.awt.Component;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.ComponentEvent;
import java.awt.event.ComponentAdapter;
import javax.swing.JPanel;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JTextField;
import javax.swing.BoxLayout;
import javax.swing.BorderFactory;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

import viewer.common.Const;
import viewer.common.Routines;
import viewer.common.Parameters;
import viewer.common.LabeledTextField;

public class RowAdjustments
{
    private static final Font      FONT              = Const.FONT;
    private static final String    ROW_COUNT_RESIZE  = "Row";
    private static final String    ROW_HEIGHT_RESIZE = "Height";

    private ViewportTimeYaxis      canvas_vport;
    private YaxisTree              tree_view;

    private JComboBox              combo_ROW_RESIZE;
    private ScaledSlider           slider_VIS_ROW_HEIGHT;
    private LabeledTextField       fld_VIS_ROW_HEIGHT;
    private ScaledSlider           slider_VIS_ROW_COUNT;
    private LabeledTextField       fld_VIS_ROW_COUNT;
    private JButton                fitall_btn;

    private JPanel                 combo_panel;
    private JPanel                 txtfld_panel;
    private JPanel                 slider_panel;
    private JPanel                 misc_panel;

    private Diagnosis              debug;

    public RowAdjustments( ViewportTimeYaxis y_vport, YaxisTree y_tree )
    {
        canvas_vport  = y_vport;
        tree_view     = y_tree;

        debug         = new Diagnosis();
        debug.setActive( false );

        combo_ROW_RESIZE  = new JComboBox();
        combo_ROW_RESIZE.setFont( FONT );
        combo_ROW_RESIZE.setEditable( false );
        combo_ROW_RESIZE.addItem( ROW_COUNT_RESIZE );
        combo_ROW_RESIZE.addItem( ROW_HEIGHT_RESIZE );
        combo_ROW_RESIZE.setToolTipText(
                         "Display mode for row adjustment control" );
        combo_ROW_RESIZE.addActionListener( new ResizeModeActionListener() );

        // For constant row height during timeline canvas resizing
        slider_VIS_ROW_HEIGHT = new ScaledSlider( ScaledSlider.VERTICAL );
        slider_VIS_ROW_HEIGHT.setLabelFormat( Const.INTEGER_FORMAT );
        slider_VIS_ROW_HEIGHT.setInverted( true );
        slider_VIS_ROW_HEIGHT.addChangeListener(
                              new RowHeightSliderListener() );

        fld_VIS_ROW_HEIGHT = new LabeledTextField( "Row Height",
                                                   Const.INTEGER_FORMAT );
        fld_VIS_ROW_HEIGHT.setToolTipText( "Row height of timeline in pixel." );
        fld_VIS_ROW_HEIGHT.setHorizontalAlignment( JTextField.CENTER );
        fld_VIS_ROW_HEIGHT.setEditable( true );
        fld_VIS_ROW_HEIGHT.addActionListener( new RowHeightTextListener() );

        // For constant visible row count during timeline canvas resizing
        slider_VIS_ROW_COUNT = new ScaledSlider( ScaledSlider.VERTICAL );
        slider_VIS_ROW_COUNT.setLabelFormat( Const.INTEGER_FORMAT );
        slider_VIS_ROW_COUNT.setInverted( false );
        slider_VIS_ROW_COUNT.addChangeListener(
                             new RowCountSliderListener() );

        fld_VIS_ROW_COUNT = new LabeledTextField( "Row Count",
                                                  "###0.0#" );
                                                  // Const.INTEGER_FORMAT );
        fld_VIS_ROW_COUNT.setToolTipText(
        "Visible row count in canvas during timeline window resizing." );
        fld_VIS_ROW_COUNT.setHorizontalAlignment( JTextField.CENTER );
        fld_VIS_ROW_COUNT.setEditable( true );
        fld_VIS_ROW_COUNT.addActionListener( new RowCountTextListener() );

        combo_panel  = new JPanel();
        combo_panel.setLayout( new BoxLayout( combo_panel, BoxLayout.X_AXIS ) );
        // combo_panel.setBorder( BorderFactory.createEtchedBorder() );
        combo_panel.add( combo_ROW_RESIZE );

        txtfld_panel = new JPanel();
        txtfld_panel.setLayout( new BoxLayout( txtfld_panel,
                                               BoxLayout.X_AXIS ) );
        txtfld_panel.setBorder( BorderFactory.createEtchedBorder() );

        slider_panel = new JPanel();
        slider_panel.setLayout( new BoxLayout( slider_panel,
                                               BoxLayout.X_AXIS ) );
        slider_panel.addComponentListener( new SliderComponentListener() );

        fitall_btn  = new JButton( "Fit All Rows" );
        fitall_btn.setFont( FONT );
        fitall_btn.setBorder( BorderFactory.createRaisedBevelBorder() );
        fitall_btn.setToolTipText(
          "Compute the optimal row height that fits all the rows "
        + "in the Timeline canvas" );
        fitall_btn.addActionListener( new ButtonActionListener() );
        fitall_btn.setAlignmentX( Component.LEFT_ALIGNMENT );

        misc_panel = new JPanel();
        misc_panel.setLayout( new BoxLayout( misc_panel, BoxLayout.Y_AXIS ) );
        // misc_panel.setBorder( BorderFactory.createEtchedBorder() );
        misc_panel.setBorder( BorderFactory.createEmptyBorder(1,1,1,1) );
        misc_panel.add( fitall_btn );
    }

    public void initYLabelTreeSize()
    {
        int avail_screen_height;
        int row_height;
        int row_count;

        tree_view.setRootVisible( Parameters.Y_AXIS_ROOT_VISIBLE );
        avail_screen_height = (int) ( Routines.getScreenSize().height
                                    * Parameters.SCREEN_HEIGHT_RATIO );
        row_count           = tree_view.getRowCount();
        row_height          = avail_screen_height / row_count;
        tree_view.setRowHeight( row_height );
        tree_view.setVisibleRowCount( row_count );
    }

    public void initSlidersAndTextFields()
    {
        int row_count   = tree_view.getRowCount();
        int row_height  = tree_view.getRowHeight();
        if ( debug.isActive() ) {
            debug.println( "initSliders: START" );
            debug.println( "initSliders: N=" + row_count
                         + ", h=" + row_height );
        }

        // Assume SliderComponentListener will resize the Timeline canvas
        // to the size = row_height * row_count
        slider_VIS_ROW_HEIGHT.setMinLabel( 0 );
        slider_VIS_ROW_HEIGHT.setMaxLabel( row_height * row_count ); 
        if ( row_count > 1 ) {
            slider_VIS_ROW_COUNT.setMinLabel( 1 );
            slider_VIS_ROW_COUNT.setMaxLabel( row_count );
        }
        else {
            slider_VIS_ROW_COUNT.setMinLabel( 0 );
            slider_VIS_ROW_COUNT.setMaxLabel( 1 );
        }

        if ( Parameters.ROW_RESIZE_MODE.equals( ROW_COUNT_RESIZE ) ) {
            combo_ROW_RESIZE.setSelectedItem( ROW_COUNT_RESIZE );
            // Set one the component, e.g. fld_VIS_ROW_COUNT, then Invoke
            // fld_VIS_ROW_COUNT's listener to set other 3 components. i.e.
            // slider_VIS_ROW_COUNT, fld_VIS_ROW_HEIGHT,& slider_VIS_ROW_HEIGHT
            fld_VIS_ROW_COUNT.setDouble( (double) row_count );
            fld_VIS_ROW_COUNT.fireActionPerformed();
        }
        else if ( Parameters.ROW_RESIZE_MODE.equals( ROW_HEIGHT_RESIZE ) ) {
            combo_ROW_RESIZE.setSelectedItem( ROW_HEIGHT_RESIZE );
            // Set one the component, e.g. fld_VIS_ROW_HEIGHT, then Invoke
            // fld_VIS_ROW_HEIGHT's listener to set other 3 components. i.e.
            // slider_VIS_ROW_HEIGHT, fld_VIS_ROW_COUNT,& slider_VIS_ROW_COUNT
            fld_VIS_ROW_HEIGHT.setDouble( (double) row_height );
            fld_VIS_ROW_HEIGHT.fireActionPerformed();
        }
        if ( debug.isActive() )
            debug.println( "initSliders: END" );
    }

    public JPanel getComboBoxPanel()
    {
        return combo_panel;
    }

    public JPanel getTextFieldPanel()
    {
        return txtfld_panel;
    }

    public JPanel getSliderPanel()
    {
        return slider_panel;
    }

    public JPanel getMiscPanel()
    {
        return misc_panel;
    }

    private void initPanelsToRowHeightMode()
    {
        txtfld_panel.removeAll();
        txtfld_panel.add( fld_VIS_ROW_HEIGHT );
        txtfld_panel.revalidate();
        txtfld_panel.repaint();
        slider_panel.removeAll();
        slider_panel.add( slider_VIS_ROW_HEIGHT );
        slider_panel.revalidate();
        slider_panel.repaint();
    }

    private void initPanelsToRowCountMode()
    {
        txtfld_panel.removeAll();
        txtfld_panel.add( fld_VIS_ROW_COUNT );
        txtfld_panel.revalidate();
        txtfld_panel.repaint();
        slider_panel.removeAll();
        slider_panel.add( slider_VIS_ROW_COUNT );
        slider_panel.revalidate();
        slider_panel.repaint();
    }

    private class ResizeModeActionListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "ResizeModeActionListener: START" );
            String resize_mode = (String) combo_ROW_RESIZE.getSelectedItem();
            if ( resize_mode.equals( ROW_COUNT_RESIZE ) ) {
                Parameters.ROW_RESIZE_MODE = ROW_COUNT_RESIZE;
                initPanelsToRowCountMode();
            }
            else if ( resize_mode.equals( ROW_HEIGHT_RESIZE ) ) {
                Parameters.ROW_RESIZE_MODE = ROW_HEIGHT_RESIZE;
                initPanelsToRowHeightMode();
            }
            if ( debug.isActive() )
                debug.println( "ResizeModeActionListener: END" );
        }
    }

        public void updateSlidersAfterTreeExpansion()
        {
            String resize_mode = (String) combo_ROW_RESIZE.getSelectedItem();
            if ( resize_mode.equals( ROW_COUNT_RESIZE ) ) {
                int row_count   = tree_view.getRowCount();
                slider_VIS_ROW_COUNT.setMaxLabel( row_count );
                fld_VIS_ROW_COUNT.fireActionPerformed();
            }
            // else if ( resize_mode.equals( ROW_HEIGHT_RESIZE ) ) {
            // }
        }

/*
    private class YLabelExpansionListener implements TreeExpansionListener
    {
        public void treeCollapsed( TreeExpansionEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "YLabelExpansionListener.treeCollapsed()" );
            updateSlidersAfterTreeExpansion();
        }

        public void treeExpanded( TreeExpansionEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "YLabelExpansionListener.treeExpanded()" );
            updateSlidersAfterTreeExpansion();
        }
    }
*/

    private class SliderComponentListener extends ComponentAdapter
    {
        public void componentResized( ComponentEvent evt )
        {
            if ( debug.isActive() ) {
                debug.println( "SliderComponentListener: START" );
                debug.println( "HEIGHT: canvas_vport.height = "
                             + canvas_vport.getHeight() );
            }
            slider_VIS_ROW_HEIGHT.setMaxLabel( canvas_vport.getHeight() );

            String resize_mode = (String) combo_ROW_RESIZE.getSelectedItem();
            if ( resize_mode.equals( ROW_COUNT_RESIZE ) ) {
                double vis_row_count, row_height;
                vis_row_count  = fld_VIS_ROW_COUNT.getDouble();
                row_height     = (double) canvas_vport.getHeight()
                               / vis_row_count;
                // slider_VIS_ROW_HEIGHT.setValLabel( row_height );
                tree_view.setRowHeight( (int) Math.round( row_height ) );
                canvas_vport.fireComponentResized();
                if ( debug.isActive() )
                    debug.println( "ROW: row_height = " + row_height );
            }
            else if ( resize_mode.equals( ROW_HEIGHT_RESIZE ) ) {
                double  row_height;
                row_height = fld_VIS_ROW_HEIGHT.getDouble();
                slider_VIS_ROW_HEIGHT.setValLabel( row_height );
            }
            if ( debug.isActive() )
                debug.println( "SliderComponentListener: END" );
        }
    }

    private class RowHeightSliderListener implements ChangeListener
    {
        public void stateChanged( ChangeEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "RowHeightSliderListener: START" );
            double tree_row_count, vport_height;
            double min_row_height, max_row_height;
            double vis_row_count, row_height;

            vis_row_count  = -1.0d;
            row_height     = slider_VIS_ROW_HEIGHT.getValLabel();
            fld_VIS_ROW_HEIGHT.setDouble( row_height );
            if ( ! slider_VIS_ROW_HEIGHT.getValueIsAdjusting() ) {
                vport_height    = (double) canvas_vport.getHeight();
                tree_row_count  = (double) tree_view.getRowCount();
                min_row_height  = vport_height / tree_row_count;
                max_row_height  = vport_height;

                // Constraint: row_height * vis_row_count = tree_view_count
                // Process the invalid values first
                if ( row_height > max_row_height ) {
                    //  Slider's range should have avoided this case already
                    row_height     = max_row_height;
                    vis_row_count  = 1.0d;
                    fld_VIS_ROW_HEIGHT.setDouble( row_height );
                    slider_VIS_ROW_HEIGHT.setValLabelFully( row_height );
                }
                else {
                    if ( row_height < min_row_height ) {
                        row_height     = min_row_height;
                        vis_row_count  = tree_row_count;
                        fld_VIS_ROW_HEIGHT.setDouble( row_height );
                        slider_VIS_ROW_HEIGHT.setValLabelFully( row_height );
                    }
                    else  // The valid range: min_ < vis_row_count < max_
                        vis_row_count  = vport_height / row_height;
                }

                slider_VIS_ROW_COUNT.setValLabel( vis_row_count );
                tree_view.setRowHeight( (int) Math.round( row_height ) );
                canvas_vport.fireComponentResized();

                if ( debug.isActive() )
                    debug.println( "RowHeightSliderListener: "
                                 + "h=" + row_height + ",N=" + vis_row_count );
            }
            if ( debug.isActive() )
                debug.println( "RowHeightSliderListener: END" );
        }
    }

    private class RowHeightTextListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "RowHeightTextListener: START" );
            double tree_row_count, vport_height;
            double min_row_height, max_row_height;
            double vis_row_count, row_height;

            vport_height    = (double) canvas_vport.getHeight();
            tree_row_count  = (double) tree_view.getRowCount();
            min_row_height  = vport_height / tree_row_count;
            max_row_height  = vport_height;

            vis_row_count   = -1.0d;
            row_height      = fld_VIS_ROW_HEIGHT.getDouble();

            // Constraint: row_height * vis_row_count = tree_view_count
            // Process the invalid values first
            if ( row_height > max_row_height ) {
                row_height     = max_row_height;
                vis_row_count  = 1.0d;
                fld_VIS_ROW_HEIGHT.setDouble( row_height );
            }
            else {
                if ( row_height < min_row_height ) {
                    row_height     = min_row_height;
                    vis_row_count  = tree_row_count;
                    fld_VIS_ROW_HEIGHT.setDouble( row_height );
                }
                else // The valid range: min_ < vis_row_count < max_
                    vis_row_count   = vport_height / row_height;
            }

            slider_VIS_ROW_HEIGHT.setValLabel( row_height );
            slider_VIS_ROW_COUNT.setValLabel( vis_row_count );
            tree_view.setRowHeight( (int) Math.round( row_height ) );
            canvas_vport.fireComponentResized();

            if ( debug.isActive() ) {
                debug.println( "RowHeightTextListener: "
                             + "h=" + row_height + ",N=" + vis_row_count );
                debug.println( "RowHeightTextListener: END" );
            }
        }
    }

    private class RowCountSliderListener implements ChangeListener
    {
        public void stateChanged( ChangeEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "RowCountSliderListener: START" );
            double min_vis_row_count, max_vis_row_count;
            double vis_row_count, row_height;

            row_height     = -1.0d;
            vis_row_count  = slider_VIS_ROW_COUNT.getValLabel();
            fld_VIS_ROW_COUNT.setDouble( vis_row_count );
            if ( ! slider_VIS_ROW_COUNT.getValueIsAdjusting() ) {
                min_vis_row_count = slider_VIS_ROW_COUNT.getMinLabel();
                max_vis_row_count = slider_VIS_ROW_COUNT.getMaxLabel();

                // Constraint: row_height * vis_row_count = tree_view_count
                // Process the invalid values first
                if ( vis_row_count > max_vis_row_count ) {
                    //  Slider's range should have avoided this case already
                    vis_row_count  = max_vis_row_count;
                    fld_VIS_ROW_COUNT.setDouble( vis_row_count );
                    slider_VIS_ROW_COUNT.setValLabelFully( vis_row_count );
                }
                else {  // if ( vis_row_count <= max_vis_row_count )
                    if ( vis_row_count < min_vis_row_count ) {
                        vis_row_count   = min_vis_row_count;
                        fld_VIS_ROW_COUNT.setDouble( vis_row_count );
                        slider_VIS_ROW_COUNT.setValLabelFully( vis_row_count );
                    }
                }

                // tree_view.setVisibleRowCount( vis_row_count );
                row_height = (double) canvas_vport.getHeight() / vis_row_count;
                slider_VIS_ROW_HEIGHT.setValLabel( row_height );
                if ( debug.isActive() )
                    debug.println( "RowCountSliderListener: "
                                 + "h=" + row_height + ",N=" + vis_row_count );
            }
            if ( debug.isActive() )
                debug.println( "RowCountSliderListener: END" );
        }
    }

    private class RowCountTextListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            if ( debug.isActive() )
                debug.println( "RowCountTextListener: START" );
            double min_vis_row_count, max_vis_row_count;
            double vis_row_count, row_height;

            min_vis_row_count = slider_VIS_ROW_COUNT.getMinLabel();
            max_vis_row_count = slider_VIS_ROW_COUNT.getMaxLabel();

            row_height        = -1;
            vis_row_count     = fld_VIS_ROW_COUNT.getDouble();

            // Constraint: row_height * vis_row_count = tree_view_count
            // Process the invalid values first
            if ( vis_row_count > max_vis_row_count ) {
                vis_row_count  = max_vis_row_count;
                fld_VIS_ROW_COUNT.setDouble( vis_row_count );
            }
            else {  // if ( vis_row_count <= max_vis_row_count )
                if ( vis_row_count < min_vis_row_count ) {
                    vis_row_count   = min_vis_row_count;
                    fld_VIS_ROW_COUNT.setDouble( vis_row_count );
                }
            }

            slider_VIS_ROW_COUNT.setValLabel( vis_row_count );
            // tree_view.setVisibleRowCount( vis_row_count );
            row_height = (double) canvas_vport.getHeight() / vis_row_count;
            slider_VIS_ROW_HEIGHT.setValLabel( row_height );
            if ( debug.isActive() ) {
                debug.println( "RowCountTextListener: "
                             + "h=" + row_height + ",N=" + vis_row_count );
                debug.println( "RowCountTextListener: END" );
            }
        }
    }

    private class ButtonActionListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            double row_height;
            row_height  = (double) canvas_vport.getHeight()
                        / tree_view.getRowCount();
            slider_VIS_ROW_HEIGHT.setValLabel( row_height );
            fld_VIS_ROW_HEIGHT.setDouble( row_height );
        }
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.util.List;
import java.util.ArrayList;
import java.net.URL;
import java.awt.Window;
import javax.swing.*;

import viewer.common.Const;
import viewer.common.Dialogs;
import viewer.common.LabeledTextField;

public class ModelTimePanel extends JPanel
                            implements TimeListener
{
    private ModelTime         model = null;

    private LabeledTextField  fld_iZoom_level;
    private LabeledTextField  fld_tGlobal_min;
    private LabeledTextField  fld_tGlobal_max;
    private LabeledTextField  fld_tView_init;
    private LabeledTextField  fld_tView_final;
    private LabeledTextField  fld_tZoom_focus;
    private LabeledTextField  fld_time_per_pixel;

    private List              vport_list;


    public ModelTimePanel( ModelTime model )
    {
        super();
        this.model         = model;
        setLayout( new BoxLayout( this, BoxLayout.X_AXIS ) );

        vport_list         = new ArrayList();

        fld_iZoom_level    = new LabeledTextField( "Zoom Level ",
                                                   Const.INTEGER_FORMAT );
        fld_iZoom_level.setEditable( false );
        fld_iZoom_level.setHorizontalAlignment( JTextField.CENTER );
        add( fld_iZoom_level ); // addSeparator();

        fld_tGlobal_min    = new LabeledTextField( "Global Min Time",
                                                   Const.PANEL_TIME_FORMAT );
        fld_tGlobal_min.setEditable( false );
        add( fld_tGlobal_min ); // addSeparator();

        fld_tView_init     = new LabeledTextField( "View  Init Time",
                                                   Const.PANEL_TIME_FORMAT );
        fld_tView_init.setEditable( true );
        add( fld_tView_init ); // addSeparator();

        fld_tZoom_focus    = new LabeledTextField( "Zoom Focus Time",
                                                   Const.PANEL_TIME_FORMAT );
        fld_tZoom_focus.setEditable( true );
        add( fld_tZoom_focus );

        fld_tView_final    = new LabeledTextField( "View Final Time",
                                                   Const.PANEL_TIME_FORMAT );
        fld_tView_final.setEditable( true );
        add( fld_tView_final ); // addSeparator();

        fld_tGlobal_max    = new LabeledTextField( "Global Max Time",
                                                   Const.PANEL_TIME_FORMAT );
        fld_tGlobal_max.setEditable( false );
        add( fld_tGlobal_max ); // addSeparator();

        fld_time_per_pixel = new LabeledTextField( "Time Per Pixel",
                                                   Const.PANEL_TIME_FORMAT );
        fld_time_per_pixel.setEditable( true );
        add( fld_time_per_pixel ); // addSeparator();

        super.setBorder( BorderFactory.createEtchedBorder() );

        // Set up ActionListeners
        ActionListener time_focus_action, time_bounds_action, time_pixel_action;
        time_focus_action = new TimeFocusActionListener( model, vport_list,
                                                         fld_tZoom_focus );
        fld_tZoom_focus.addActionListener( time_focus_action );
        time_bounds_action = new TimeBoundsActionListener( model, vport_list,
                                                           fld_tView_init,
                                                           fld_tView_final );
        fld_tView_init.addActionListener( time_bounds_action );
        fld_tView_final.addActionListener( time_bounds_action );
        time_pixel_action = new TimePixelActionListener( model, vport_list,
                                                         fld_time_per_pixel );
        fld_time_per_pixel.addActionListener( time_pixel_action );
    }

    public void addViewportTime( final ViewportTime  vport )
    {
        if ( vport != null )
            vport_list.add( vport );
    }

    public void zoomLevelChanged()
    {
        fld_iZoom_level.setInteger( model.getZoomLevel() );
    }

    /*
        timeChanged() is invoked by ModelTime's updateParamDisplay()
    */
    public void timeChanged( TimeEvent evt )
    {
        if ( Debug.isActive() )
            Debug.println( "ModelTimePanel: timeChanged()'s START: " );
        fld_tGlobal_min.setDouble( model.getTimeGlobalMinimum() );
        fld_tView_init.setDouble( model.getTimeViewPosition() );
        fld_tZoom_focus.setDouble( model.getTimeZoomFocus() );
        fld_tView_final.setDouble( model.getTimeViewPosition()
                                 + model.getTimeViewExtent() );
        fld_tGlobal_max.setDouble( model.getTimeGlobalMaximum() );
        fld_time_per_pixel.setDouble( 1.0d/model.getViewPixelsPerUnitTime() );
        if ( Debug.isActive() )
            Debug.println( "ModelTimePanel: timeChanged()'s END: " );
    }



    private class TimeFocusActionListener implements ActionListener
    {  
        private ModelTime         time_model      = null;
        private List              vport_list      = null;
        private LabeledTextField  fld_tZoom_focus = null;

        public TimeFocusActionListener( ModelTime model, List vports,
                                        LabeledTextField field_tZoom_focus )
        {
            time_model      = model;
            vport_list      = vports;
            fld_tZoom_focus = field_tZoom_focus;
        }

        public void actionPerformed( ActionEvent evt )
        {
            Window  win;
            double  tView_focus;
            tView_focus = fld_tZoom_focus.getDouble();
            if (    tView_focus < time_model.getTimeGlobalMinimum()
                 || tView_focus > time_model.getTimeGlobalMaximum() ) { 
                win = SwingUtilities.windowForComponent( ModelTimePanel.this );
                Dialogs.error( win,
                               "Zoom Focus Time is out of the range of the\n"
                             + "Global Min/Max Times! Restore the old value." );
                fld_tZoom_focus.setDouble( time_model.getTimeZoomFocus() );
                return;
            }

            ViewportTime  vport;
            time_model.setTimeZoomFocus( tView_focus );
            for ( int idx = vport_list.size()-1; idx >=0; idx-- ) {
                vport  = (ViewportTime) vport_list.get( idx );
                vport.repaint();
            }
        }
    }

    private class TimeBoundsActionListener implements ActionListener
    {  
        private ModelTime         time_model      = null;
        private List              vport_list      = null;
        private LabeledTextField  fld_tView_init  = null;
        private LabeledTextField  fld_tView_final = null;

        public TimeBoundsActionListener( ModelTime  model, List vports,
                                         LabeledTextField field_tView_init,
                                         LabeledTextField field_tView_final )
        {
            time_model       = model;
            vport_list       = vports;
            fld_tView_init   = field_tView_init;
            fld_tView_final  = field_tView_final; 
        }

        public void actionPerformed( ActionEvent evt )
        {
            Window  win;
            double  tView_init, tView_final;
            boolean isOK = true;

            tView_init   = fld_tView_init.getDouble();
            if (    tView_init < time_model.getTimeGlobalMinimum()
                 || tView_init > time_model.getTimeGlobalMaximum() ) { 
                win = SwingUtilities.windowForComponent( ModelTimePanel.this );
                Dialogs.error( win,
                               "View Init Time is out of the range of the\n"
                             + "Global Min/Max Times! Restore the old value." );
                fld_tView_init.setDouble( time_model.getTimeViewPosition() );
                isOK = false;
            }

            tView_final  = fld_tView_final.getDouble();
            if (    tView_final < time_model.getTimeGlobalMinimum()
                 || tView_final > time_model.getTimeGlobalMaximum() ) {
                win = SwingUtilities.windowForComponent( ModelTimePanel.this );
                Dialogs.error( win,
                               "View Final Time is out of the range of the\n"
                             + "Global Min/Max Times! Restore the old value." );
                fld_tView_final.setDouble( time_model.getTimeViewPosition()
                                         + time_model.getTimeViewExtent() );
                isOK = false;
            }

            if ( tView_init >= tView_final ) {
                win = SwingUtilities.windowForComponent( ModelTimePanel.this );
                Dialogs.error( win,
                               "View Init Time is later or equal to\n"
                             + "View Final Time!  Restore the old value." );
                fld_tView_init.setDouble( time_model.getTimeViewPosition() );
                fld_tView_final.setDouble( time_model.getTimeViewPosition()
                                         + time_model.getTimeViewExtent() );
                isOK = false;
            }

            if ( ! isOK )
                return;

            double  tView_extent, tView_focus;

            tView_extent = tView_final - tView_init;
            time_model.zoomRapidly( tView_init, tView_extent );
            tView_focus  = ( tView_init + tView_final ) / 2.0d; 
            time_model.setTimeZoomFocus( tView_focus );

            ViewportTime  vport;
            for ( int idx = vport_list.size()-1; idx >=0; idx-- ) {
                vport  = (ViewportTime) vport_list.get( idx );
                vport.repaint();
                vport.resetToolBarZoomButtons();
            }
        }
    }

    private class TimePixelActionListener implements ActionListener
    {
        private ModelTime         time_model      = null;
        private List              vport_list      = null;
        private LabeledTextField  fld_time_pixel  = null;

        public TimePixelActionListener( ModelTime  model, List vports,
                                        LabeledTextField field_time_pixel )
        {
            time_model       = model;
            vport_list       = vports;
            fld_time_pixel   = field_time_pixel;
        }

        public void actionPerformed( ActionEvent evt )
        {
            Window  win;
            if ( fld_time_pixel.getDouble() <= 0.0d ) {
                win = SwingUtilities.windowForComponent( ModelTimePanel.this );
                Dialogs.error( win,
                               "Time Per Pixel is less than or equal to 0.0!\n"
                             + "Restore the old value." );
                fld_time_pixel.setDouble(
                               1.0d / time_model.getViewPixelsPerUnitTime() );
                return;
            }
            
            double   tView_init, tView_final, tView_extent, tView_focus;

            tView_extent = time_model.computeTimeViewExtent(
                                             fld_time_pixel.getDouble() );
            tView_focus  = time_model.getTimeZoomFocus();
            tView_init   = tView_focus - tView_extent / 2.0d;
            time_model.zoomRapidly( tView_init, tView_extent );

            ViewportTime  vport;
            for ( int idx = vport_list.size()-1; idx >=0; idx-- ) {
                vport  = (ViewportTime) vport_list.get( idx );
                vport.repaint();
                vport.resetToolBarZoomButtons();
            }
        }
    }

}

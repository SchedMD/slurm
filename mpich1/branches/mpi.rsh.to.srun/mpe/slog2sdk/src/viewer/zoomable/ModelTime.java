/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.Stack;
import java.awt.Window;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.*;

import base.drawable.TimeBoundingBox;
import viewer.common.Parameters;
import viewer.common.Dialogs;

/*
   This is the Model( as in MVC architecture ) that determines the 
   transformation between user __time__ coordinates and the graphics 
   __pixel__ coordinates.  Since this classs is extended from the 
   DefaultBoundedRangeModel class which is then being used as 
   the model INSIDE the ScrollbarTime, JScrollBar, so the __pixel__ 
   interface of this class is synchronized with that of ScrollbarTime 
   constantly during the program execution.  i.e.  
      ModelTime.getMinimum()  == ScrollbarTime.getMinimum()
      ModelTime.getMaximum()  == ScrollbarTime.getMaximum()
      ModelTime.getValue()    == ScrollbarTime.getValue()
      ModelTime.getExtent()   == ScrollbarTime.getExtent()
   All( or most ) accesses to this class should be through 
   the __time__ interface.  The only class that needs to access
   the __pixel__ interface of this classs is the VIEW object
   of the ViewportTime class.  i.e. method like
      setViewPixelsPerUnitTime()
      getViewPixelsPerUnitTime()
      updatePixelCoords()
   are accessed by RulerTime class.
*/
public class ModelTime extends DefaultBoundedRangeModel
                       implements AdjustmentListener
{
    // private int    MAX_SCROLLBAR_PIXELS = Integer.MAX_VALUE;
    /*
        If DefaultBoundedRangeModel is used,
        MAX_SCROLLBAR_PIXELS < Integer.MAX_VALUE / 2;
        otherwise there will be overflow of DefaultBoundedRangeModel
        when moving 1 block increment of scrollbar when zoom_level = 1
    */
    private int    MAX_SCROLLBAR_PIXELS = 1073741824;  // = 2^(30)

    // user coordinates of the time axis of the viewport
    private double tGlobal_min;
    private double tGlobal_max;
    private double tGlobal_extent;
    private double tView_init;
    private double tView_extent;

    // previous value of tView_init;
    private double old_tView_init;

    // the zoom focus in user coodinates along the time axis of the viewport
    private double tZoom_focus;
    // the zoom focus in graphics pixel coodinates
    private int    iZoom_focus;

    // pixel coordinates of the time axis of the viewport
    // are buried inside the superclass DefaultBoundedRangeModel

    // screen properties
    private int    iView_width = -1;
    private double iViewPerTime;         // No. of View pixel per unit time
    private double iScrollbarPerTime;    // No. of ScrollBar pixel per unit time
    private double tZoomScale  = 1.0d;
    private double tZoomFactor;
    private double logZoomFactor;;

    // special purpose ChangeListeners, TimeListeners, to avoid conflict with
    // the EventListenerList, listenerList, in DefaultBoundedRangeModel
    private ModelTimePanel     params_display;
    private EventListenerList  time_listener_list;
    // internal global variable for use in fireTimeChanged()
    private TimeEvent          time_chg_evt;

    private Window             root_window;
    private JScrollBar         scrollbar;

    private Stack              zoom_undo_stack;
    private Stack              zoom_redo_stack;

    public ModelTime( final Window  top_window,
                            double  init_global_time,
                            double  final_global_time )
    {
        params_display     = null;
        time_chg_evt       = null;
        time_listener_list = new EventListenerList();
        zoom_undo_stack    = new Stack();
        zoom_redo_stack    = new Stack();

        root_window        = top_window;
        setTimeGlobalMinimum( init_global_time );
        setTimeGlobalMaximum( final_global_time );
        setTimeZoomFocus();
        tZoomFactor        = 2.0d;
        logZoomFactor      = Math.log( tZoomFactor );
    }

    /*
        None of the setTimeXXXXXX() functions updates the __Pixel__ coordinates
    */
    private void setTimeGlobalMinimum( double init_global_time )
    {
        tGlobal_min    = init_global_time;
        old_tView_init = tGlobal_min;
        tView_init     = tGlobal_min;
    }

    private void setTimeGlobalMaximum( double final_global_time )
    {
        tGlobal_max    = final_global_time;
        tGlobal_extent = tGlobal_max - tGlobal_min;
        tView_extent   = tGlobal_extent;
        iScrollbarPerTime = (double) MAX_SCROLLBAR_PIXELS / tGlobal_extent;
    }

    // tGlobal_min & tGlobal_max cannot be changed by setTimeViewPosition()
    private void setTimeViewPosition( double cur_view_init )
    {
        old_tView_init = tView_init;
        if ( cur_view_init < tGlobal_min )
            tView_init     = tGlobal_min;
        else {
            if ( cur_view_init > tGlobal_max - tView_extent )
                tView_init     = tGlobal_max - tView_extent;
            else
                tView_init     = cur_view_init;
        }
    }

    // tGlobal_min & tGlobal_max cannot be changed by setTimeViewExtent()
    private void setTimeViewExtent( double cur_view_extent )
    {
        if ( cur_view_extent < tGlobal_extent ) {
            tView_extent   = cur_view_extent;
            if ( tView_init  > tGlobal_max - tView_extent ) {
                old_tView_init  = tView_init;
                tView_init      = tGlobal_max - tView_extent;
            }
        }
        else {
            tView_extent   = tGlobal_extent;
            old_tView_init = tView_init;
            tView_init     = tGlobal_min;
        }
    }

    public double getTimeGlobalMinimum()
    {
        return tGlobal_min;
    }

    public double getTimeGlobalMaximum()
    {
        return tGlobal_max;
    }

    public double getTimeViewPosition()
    {
        return tView_init;
    }

    public double getTimeViewExtent()
    {
        return tView_extent;
    }

    /*
       Set the Number of Pixels in the Viewport window.
       if iView_width is NOT set, pixel coordinates cannot be updated.
    */
    public void setViewPixelsPerUnitTime( int width )
    {
        iView_width  = width;
        iViewPerTime = iView_width / tView_extent;
    }

    public double getViewPixelsPerUnitTime()
    {
        return iViewPerTime;
    }

    public double computeTimeViewExtent( double time_per_pixel )
    {
        return iView_width * time_per_pixel;
    }

    /*
       +1 : moving to the future, i.e. right
       -1 : moving to the past  , i.e. left
    */
    public int getViewportMovingDir()
    {
        double tView_init_changed = tView_init - old_tView_init;
        if ( tView_init_changed > 0.0 )
            return 1;
        else if ( tView_init_changed < 0.0 )
            return -1;
        else
            return 0;
    }

    public void setScrollBar( JScrollBar sb )
    {
        scrollbar = sb;
    }

    public void removeScrollBar()
    {
        scrollbar = null;
    }

    public void setParamDisplay( ModelTimePanel tl )
    {
        params_display = tl;
    }

    public void removeParamDisplay( ModelTimePanel tl )
    {
        params_display = null;
    }

    private void updateParamDisplay()
    {
        if ( params_display != null ) {
            if ( time_chg_evt == null )
                time_chg_evt = new TimeEvent( this );
            params_display.timeChanged( time_chg_evt );
            params_display.zoomLevelChanged();
        }
    }

    public void addTimeListener( TimeListener tl )
    {
        time_listener_list.add( TimeListener.class, tl );
    }

    public void removeTimeListener( TimeListener tl )
    {
        time_listener_list.remove( TimeListener.class, tl );
    }

    // Notify __ALL__ listeners that have registered interest for
    // notification on this event type.  The event instance 
    // is lazily created using the parameters passed into 
    // the fire method.
    protected void fireTimeChanged()
    {
        // Guaranteed to return a non-null array
        Object[] listeners = time_listener_list.getListenerList();
        // Process the listeners last to first, notifying
        // those that are interested in this event
        for ( int i = listeners.length-2; i>=0; i-=2 ) {
            if ( listeners[i] == TimeListener.class ) {
                // Lazily create the event:
                if ( time_chg_evt == null )
                    time_chg_evt = new TimeEvent( this );
                ( (TimeListener) listeners[i+1] ).timeChanged( time_chg_evt );
            }
        }
    }

    /*
       time2pixel() and pixel2time() are local to this object.
       They have nothing to do the ones in ScrollableObject
       (i.e. RulerTime/CanvasXXXXline).  In general, no one but
       this object and possibly ScrollbarTime needs to access
       the following time2pixel() and pixel2time() because the
       ratio to flip between pixel and time is related to scrollbar.
    */
    private int time2pixel( double time_coord )
    {
        return (int) Math.round( ( time_coord - tGlobal_min )
                               * iScrollbarPerTime );
    }

    private double pixel2time( int pixel_coord )
    {
        return (double) pixel_coord / iScrollbarPerTime
                      + tGlobal_min;
    }

    private int timeRange2pixelRange( double time_range )
    {
        return (int) Math.round( time_range * iScrollbarPerTime );
    }

    private double pixelRange2timeRange( int pixel_range )
    {
        return (double) pixel_range / iScrollbarPerTime;
    }

    public void updatePixelCoords()
    {
        // super.setRangeProperties() calls super.fireStateChanged();
        super.setRangeProperties( time2pixel( tView_init ),
                                  timeRange2pixelRange( tView_extent ),
                                  time2pixel( tGlobal_min ),
                                  time2pixel( tGlobal_max ),
                                  super.getValueIsAdjusting() );
        // fireTimeChanged();
    }

    public void updateTimeCoords()
    {
        if ( iScrollbarPerTime > 0 ) {
            tGlobal_min    = pixel2time( super.getMinimum() );
            tGlobal_max    = pixel2time( super.getMaximum() );
            tGlobal_extent = tGlobal_max - tGlobal_min;
            old_tView_init = tView_init;
            tView_init     = pixel2time( super.getValue() );
            tView_extent   = pixelRange2timeRange( super.getExtent() );
            updateParamDisplay();
        }
    }

    /*
        set/get Zoom Factor
    */
    public void setTimeZoomFactor( double inTimeZoomFactor )
    {
        tZoomFactor    = inTimeZoomFactor;
        logZoomFactor  = Math.log( tZoomFactor );
    }

    public double getTimeZoomFactor()
    {
        return tZoomFactor;
    }

    /*
        set/get Zoom Focus
    */
    public void setTimeZoomFocus()
    {
        tZoom_focus = tView_init + tView_extent / 2.0;    
    }

    public void setTimeZoomFocus( double inTimeZoomFocus )
    {
        tZoom_focus = inTimeZoomFocus;
        updateParamDisplay();
    }

    public double getTimeZoomFocus()
    {
        return tZoom_focus;
    }

    /*
        Zoom Level
    */
    public int getZoomLevel()
    {
        return (int) Math.round( Math.log( tZoomScale ) / logZoomFactor );
    }

    private void setScrollBarIncrements()
    {
        /*
            This needs to be called after updatePixelCoords()
        */
        int sb_block_incre, sb_unit_incre;
        if ( scrollbar != null ) {
            sb_block_incre = super.getExtent();
            if ( sb_block_incre <= 0 ) {
                Dialogs.error( root_window,
                               "You have reached the Zoom limit! "
                             + "Time ScrollBar has 0 BLOCK Increment. "
                             + "Zoom out or risk crashing the viewer." );
                sb_block_incre = 0;
            }
            scrollbar.setBlockIncrement( sb_block_incre );
            sb_unit_incre  =  timeRange2pixelRange( tView_extent
                                       * Parameters.TIME_SCROLL_UNIT_RATIO );
            if ( sb_unit_incre <= 0 ) {
                Dialogs.error( root_window,
                               "You have reached the Zoom limit! "
                             + "Time ScrollBar has 0 UNIT Increment. "
                             + "Zoom out or risk crashing the viewer." );
                sb_unit_incre = 0;
            }
            scrollbar.setUnitIncrement( sb_unit_incre );
        }
    }

    // tView_change is  the time measured in second.
    public void scroll( double tView_change )
    {
        this.setTimeViewPosition( tView_init + tView_change );
        this.updatePixelCoords();
        // this.setScrollBarIncrements();
    }

    // iView_change is measured in image or viewport pixel coordinates in pixel.
    // NOT measured in scrollbar's model, ie DefaultBoundRangeModel, coodinates
    public void scroll( int iView_change )
    {
        double tView_change = (double) iView_change /iViewPerTime;
        this.scroll( tView_change );
    }

    // iView_change is measured in image or viewport pixel coordinates in pixel.
    // The following function allows scroll pass tGlobal_min and tGlobal_max.
    // In general, it is not desirable, so Avoid using this scroll() function.
    public void scroll( int iView_change, boolean isValueAdjusting )
    {
        old_tView_init = tView_init;
        double tView_change = (double) iView_change / iViewPerTime;
        int iScrollbar_change = this.timeRange2pixelRange( tView_change );
        super.setRangeProperties( super.getValue() + iScrollbar_change,
                                  super.getExtent(),
                                  super.getMinimum(),
                                  super.getMaximum(),
                                  isValueAdjusting );
        tView_init     = pixel2time( super.getValue() );
        updateParamDisplay();
    }



    /*
        Zoom Operations
    */
    public void zoomHome()
    {
        tZoomScale  = 1.0;
        this.setTimeViewExtent( tGlobal_extent );
        this.setTimeViewPosition( tGlobal_min );

        iViewPerTime = iView_width / tView_extent;
        this.updatePixelCoords();
        this.setScrollBarIncrements();

        // clean up all the zoom stacks.
        zoom_undo_stack.clear();
        zoom_redo_stack.clear();
    }

    private void updateZoomStack( Stack zoom_stack )
    {
        TimeBoundingBox vport_timebox;
        vport_timebox = new TimeBoundingBox();
        vport_timebox.setEarliestTime( tView_init );
        vport_timebox.setLatestFromEarliest( tView_extent );
        zoom_stack.push( vport_timebox );
    }

    /*
        Zoom In/Out operations:

        (tView_init , tView_extent ) are before Zoom In/Out operations
        (tView_init^, tView_extent^) are after  Zoom In/Out operations

        where user clicks should be constant before & after zoom in/out,
        define  tView_ratio = ( tView_center - tView_init ) / tView_extent
        e.g. if tView_center is the middle of viewport, tView_ratio = 1/2

           tView_init + tView_extent * tView_ratio
         = tView_init^ + tView_extent^ * tView_ratio
         = constant

        when tView_focus is within viewport
            constant = tView_focus
        else
            constant = middle of the viewport
    */
    public void zoomIn()
    {
        this.updateZoomStack( zoom_undo_stack );

        double tZoom_center;
        double tView_ratio;

        tZoomScale  *= tZoomFactor;
        if (    tView_init  < tZoom_focus
             && tZoom_focus < tView_init + tView_extent )
            tZoom_center = tZoom_focus;
        else
            tZoom_center = tView_init + tView_extent / 2.0;
        tView_ratio  = ( tZoom_center - tView_init ) / tView_extent;
        this.setTimeViewExtent( tView_extent / tZoomFactor );
        this.setTimeViewPosition( tZoom_center - tView_extent * tView_ratio );

        iViewPerTime = iView_width / tView_extent;
        this.updatePixelCoords();
        this.setScrollBarIncrements();
    }

    public void zoomOut()
    {
        this.updateZoomStack( zoom_undo_stack );

        double tZoom_center;
        double tView_ratio;

        tZoomScale  /= tZoomFactor;
        if (    tView_init  < tZoom_focus
             && tZoom_focus < tView_init + tView_extent )
            tZoom_center = tZoom_focus;
        else
            tZoom_center = tView_init + tView_extent / 2.0;
        tView_ratio  = ( tZoom_center - tView_init ) / tView_extent;
        this.setTimeViewExtent( tView_extent * tZoomFactor );
        this.setTimeViewPosition( tZoom_center - tView_extent * tView_ratio );

        iViewPerTime = iView_width / tView_extent;
        this.updatePixelCoords();
        this.setScrollBarIncrements();
    }

    public void zoomRapidly( double new_tView_init, double new_tView_extent )
    {
        double cur_tZoomScale;
        cur_tZoomScale = tView_extent / new_tView_extent;

        // If this is a rapid zoom-in
        // if ( cur_tZoomScale > 1.0d )
            this.updateZoomStack( zoom_undo_stack );

        tZoomScale  *= cur_tZoomScale;
        this.setTimeViewExtent( new_tView_extent );
        this.setTimeViewPosition( new_tView_init );

        iViewPerTime = iView_width / tView_extent;
        this.updatePixelCoords();
        this.setScrollBarIncrements();
    }

    private void zoomBack( double new_tView_init, double new_tView_extent )
    {
        double cur_tZoomScale;
        cur_tZoomScale = tView_extent / new_tView_extent;

        tZoomScale  *= cur_tZoomScale;
        this.setTimeViewExtent( new_tView_extent );
        this.setTimeViewPosition( new_tView_init );

        iViewPerTime = iView_width / tView_extent;
        this.updatePixelCoords();
        this.setScrollBarIncrements();
    }

    public void zoomUndo()
    {
        if ( ! zoom_undo_stack.empty() ) {
            this.updateZoomStack( zoom_redo_stack );
            TimeBoundingBox vport_timebox;
            vport_timebox = (TimeBoundingBox) zoom_undo_stack.pop();
            this.zoomBack( vport_timebox.getEarliestTime(),
                           vport_timebox.getDuration() );
            vport_timebox = null;
        }
    }

    public void zoomRedo()
    {
        if ( ! zoom_redo_stack.empty() ) {
            this.updateZoomStack( zoom_undo_stack );
            TimeBoundingBox vport_timebox;
            vport_timebox = (TimeBoundingBox) zoom_redo_stack.pop();
            this.zoomBack( vport_timebox.getEarliestTime(),
                           vport_timebox.getDuration() );
            vport_timebox = null;
        }
    }

    public boolean isZoomUndoStackEmpty()
    {
        return zoom_undo_stack.empty();
    }

    public boolean isZoomRedoStackEmpty()
    {
        return zoom_redo_stack.empty();
    }

    // fire StateChanged for specific listener class.
    /*
    public void fireStateChanged( String listenerClass )
    {
        // listenersList is defined in superclass DefaultBoundedRangeModel
        Object[] listeners = listenerList.getListenerList();
        for ( int i = listeners.length - 2; i >= 0; i -=2 ) {
            if (  listeners[i] == ChangeListener.class
               && listeners[i+1].getClass().getName().equals(listenerClass) ) {
                if ( Debug.isActive() )
                    Debug.println( "ModelTime: fireStateChanged()'s "
                                 + "listeners[" + (i+1) + "] = "
                                 + listeners[i+1].getClass().getName() );
                if (changeEvent == null) {
                    changeEvent = new ChangeEvent(this);
                }
                ((ChangeListener)listeners[i+1]).stateChanged(changeEvent);
            }

            if ( Debug.isActive() && listeners[i] == ChangeListener.class )
                Debug.println( "ModelTime: fireStateChanged()'s "
                             + "ChangeListeners[" + (i+1) + "] = "
                             + listeners[i+1].getClass().getName() );
        }
    }
    */

    public void adjustmentValueChanged( AdjustmentEvent evt )
    {
        if ( Debug.isActive() ) {
            Debug.println( "ModelTime: AdjustmentValueChanged()'s START: " );
            Debug.println( "adj_evt = " + evt );
        }

        if ( Debug.isActive() )
            Debug.println( "ModelTime(before) = " + this.toString() );
        this.updateTimeCoords();
        if ( Debug.isActive() )
            Debug.println( "ModelTime(after) = " + this.toString() );

        // notify all TimeListeners of changes from Adjustment Listener
        this.fireTimeChanged();
        if ( Debug.isActive() )
            Debug.println( "ModelTime: AdjustmentValueChanged()'s END: " );
    }

    public String toString()
    {
        String str_rep = super.toString() + ",  "
                       + "tGlobal_min=" + tGlobal_min + ", "
                       + "tGlobal_max=" + tGlobal_max + ", "
                       + "tView_init=" + tView_init + ", "
                       + "tView_extent=" + tView_extent + ", "
                       + "iView_width=" + iView_width + ", "
                       + "iViewPerTime=" + iViewPerTime + ", "
                       + "iScrollbarPerTime=" + iScrollbarPerTime
                       ;

        return getClass().getName() + "{" + str_rep + "}";
    }
}

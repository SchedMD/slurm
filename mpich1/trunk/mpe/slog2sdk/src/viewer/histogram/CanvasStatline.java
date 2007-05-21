/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.histogram;

import java.util.Date;
import java.util.Map;
import java.util.Iterator;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.*;
import javax.swing.tree.TreePath;

import base.drawable.TimeBoundingBox;
import base.statistics.Summarizable;
import base.statistics.BufForTimeAveBoxes;
import logformat.slog2.LineIDMap;
import viewer.common.Dialogs;
import viewer.common.Routines;
import viewer.common.Parameters;
import viewer.common.CustomCursor;
import viewer.zoomable.Debug;
import viewer.zoomable.Profile;
import viewer.zoomable.ModelTime;
import viewer.zoomable.YaxisMaps;
import viewer.zoomable.YaxisTree;
import viewer.zoomable.InfoDialog;
import viewer.zoomable.CoordPixelImage;
import viewer.zoomable.ScrollableObject;

public class CanvasStatline extends ScrollableObject
{
    private static final int            MIN_VISIBLE_ROW_COUNT = 2;
    private static final boolean        IS_INCRE_STARTTIME    = true;
    private static       GradientPaint  BackgroundPaint       = null;

    private BufForTimeAveBoxes buf4statboxes;
    private YaxisMaps          y_maps;
    private YaxisTree          tree_view;
    private BoundedRangeModel  y_model;
    private String[]           y_colnames;

    private Dialog             root_dialog;
    private TimeBoundingBox    timeframe4imgs;   // TimeFrame for images[]

    private ChangeListener     change_listener;
    private ChangeEvent        change_event;

    private int                num_rows;
    private int                row_height;
    private Map                map_line2row;

    private Date               zero_time, init_time, final_time;


    public CanvasStatline( ModelTime           time_model,
                           BufForTimeAveBoxes  statboxes, 
                           BoundedRangeModel   yaxis_model,
                           YaxisMaps           yaxis_maps,
                           String[]            yaxis_colnames )
    {
        super( time_model );

        buf4statboxes   = statboxes;
        y_maps          = yaxis_maps;
        tree_view       = y_maps.getTreeView();
        y_model         = yaxis_model;
        y_colnames      = yaxis_colnames;
        map_line2row    = null;
        // timeframe4imgs to be initialized later in initializeAllOffImages()
        timeframe4imgs  = null;

        root_dialog     = null;
        change_event    = null;
        change_listener = null;

        // if ( BackgroundPaint == null )
        //     BackgroundPaint = new GradientPaint( 0, 0, Color.black,
        //                                          5, 5, Color.gray, true );
    }

    public void addChangeListener( ChangeListener listener )
    {
        change_event    = new ChangeEvent( this );
        change_listener = listener; 
    }

    public Dimension getMinimumSize()
    {
        // int  min_view_height = MIN_VISIBLE_ROW_COUNT
        //                      * Parameters.Y_AXIS_ROW_HEIGHT;
        int  min_view_height = 0;
        //  the width below is arbitary
        if ( Debug.isActive() )
            Debug.println( "CanvasStatline: min_size = "
                         + "(0," + min_view_height + ")" );
        return new Dimension( 0, min_view_height );
    }

    public Dimension getMaximumSize()
    {
        if ( Debug.isActive() )
            Debug.println( "CanvasStatline: max_size = "
                         + "(" + Short.MAX_VALUE
                         + "," + Short.MAX_VALUE + ")" );
        return new Dimension( Short.MAX_VALUE, Short.MAX_VALUE );
    }

    public int getJComponentHeight()
    {
        int rows_size = tree_view.getRowCount() * tree_view.getRowHeight();
        int view_size = y_model.getMaximum() - y_model.getMinimum() + 1;
        if ( view_size > rows_size )
            return view_size;
        else
            return rows_size;
    }

    private void fireChangeEvent()
    {
        if ( change_event != null )
            change_listener.stateChanged( change_event );
    }

    protected void initializeAllOffImages( final TimeBoundingBox imgs_times )
    {
        if ( Profile.isActive() )
            zero_time = new Date();

        if ( root_dialog == null )
            root_dialog  = (Dialog) SwingUtilities.windowForComponent( this );
        if ( timeframe4imgs == null )
            timeframe4imgs = new TimeBoundingBox( imgs_times );

        Routines.setComponentAndChildrenCursors( root_dialog,
                                                 CustomCursor.Wait );
        num_rows    = tree_view.getRowCount();
        row_height  = tree_view.getRowHeight();

        if ( Profile.isActive() )
            init_time = new Date();

        map_line2row = y_maps.getMapOfLineIDToRowID();
        if ( map_line2row == null ) {
            if ( ! y_maps.update() )
                Dialogs.error( root_dialog, "Error in updating YaxisMaps!" );
            map_line2row = y_maps.getMapOfLineIDToRowID();
        }
        // System.out.println( "map_line2row = " + map_line2row );
    }

    protected void finalizeAllOffImages( final TimeBoundingBox imgs_times )
    {
        map_line2row = null;
        // Update the timeframe of all images
        timeframe4imgs.setEarliestTime( imgs_times.getEarliestTime() );
        timeframe4imgs.setLatestTime( imgs_times.getLatestTime() );
        this.fireChangeEvent();  // to update TreeTrunkPanel.
        Routines.setComponentAndChildrenCursors( root_dialog,
                                                 CustomCursor.Normal );

        if ( Profile.isActive() )
            final_time = new Date();
        if ( Profile.isActive() )
            Profile.println( "CanvasStatline.finalizeAllOffImages(): "
                           + "init. time = "
                           + (init_time.getTime() - zero_time.getTime())
                           + " msec.,   total time = "
                           + (final_time.getTime() - zero_time.getTime())
                           + " msec." );
    }

    protected void drawOneOffImage(       Image            offImage,
                                    final TimeBoundingBox  timebounds )
    {
        if ( Debug.isActive() )
            Debug.println( "CanvasStatline: drawOneOffImage()'s offImage = "
                         + offImage );
        if ( offImage != null ) {
            // int offImage_width = visible_size.width * NumViewsPerImage;
            int        offImage_width  = offImage.getWidth( this );
            int        offImage_height = offImage.getHeight( this ); 
            Graphics2D offGraphics     = (Graphics2D) offImage.getGraphics();

            // Set RenderingHint to have MAX speed.
            offGraphics.setRenderingHint( RenderingHints.KEY_RENDERING,
                                          RenderingHints.VALUE_RENDER_SPEED );

            // offGraphics.getClipBounds() returns null
            // offGraphics.setClip( 0, 0, getWidth()/NumImages, getHeight() );
            // Do the ruler labels in a small font that's black.
            Color back_color = (Color) Parameters.BACKGROUND_COLOR.toValue();
            offGraphics.setPaint( back_color );
            offGraphics.fillRect( 0, 0, offImage_width, offImage_height );

            int    irow;
            int    i_Y;

            CoordPixelImage coord_xform;  // local Coordinate Transform
            coord_xform = new CoordPixelImage( this, row_height, timebounds );

            // Set AntiAliasing OFF for all the horizontal and vertical lines
            offGraphics.setRenderingHint( RenderingHints.KEY_ANTIALIASING,
                                          RenderingHints.VALUE_ANTIALIAS_OFF );

            // Draw the center TimeLines.
            offGraphics.setColor( Color.cyan );
            for ( irow = 0 ; irow < num_rows ; irow++ ) {
                //  Select only non-expanded row
                if ( ! tree_view.isExpanded( irow ) ) {
                    i_Y = coord_xform.convertRowToPixel( (float) irow );
                    offGraphics.drawLine( 0, i_Y, offImage_width-1, i_Y );
                }
            }

            // Draw the image separator when in Debug or Profile mode
            if ( Debug.isActive() || Profile.isActive() ) {
                offGraphics.setColor( Color.gray );
                offGraphics.drawLine( 0, 0, 0, this.getHeight() );
            }



            buf4statboxes.initializeDrawing( map_line2row, back_color,
                                             Parameters.HISTOGRAM_ZERO_ORIGIN,
                                             Parameters.STATE_HEIGHT_FACTOR,
                                             Parameters.NESTING_HEIGHT_FACTOR );
            int N_nestable = 0, N_nestless = 0;

            N_nestable = buf4statboxes.drawAllStates( offGraphics,
                                                      coord_xform );

            // Set AntiAliasing from Parameters for all slanted lines
            offGraphics.setRenderingHint( RenderingHints.KEY_ANTIALIASING,
                                    Parameters.ARROW_ANTIALIASING.toValue() );
            N_nestless = buf4statboxes.drawAllArrows( offGraphics,
                                                      coord_xform );

            if ( Profile.isActive() )
                Profile.println( "CanvasStatline.drawOneOffImage(): "
                               + "N_NestAble = " + N_nestable + ",  "
                               + "R_NestLess = " + N_nestless );

            offGraphics.dispose();
        }
    }   // endof drawOneOffImage()

    public InfoDialog getPropertyAt( final Point            local_click,
                                     final TimeBoundingBox  vport_timeframe )
    {

        /* System.out.println( "\nshowPropertyAt() " + local_click ); */
        CoordPixelImage coord_xform;  // Local Coordinate Transform
        coord_xform = new CoordPixelImage( this, row_height, 
                                           super.getTimeBoundsOfImages() );
        double clicked_time = coord_xform.convertPixelToTime( local_click.x );

        // Determine the timeframe of the current view by vport_timeframe
        // System.out.println( "CurrView's timeframe = " + vport_timeframe );

        Summarizable  clicked_summary;
        clicked_summary = buf4statboxes.getSummarizableAt( coord_xform,
                                                           local_click );
        if ( clicked_summary != null) {
            return new InfoDialogForSummary( root_dialog, clicked_time,
                                             tree_view, y_colnames,
                                             clicked_summary );
        }

        return super.getTimePropertyAt( local_click );
    }   // endof  getPropertyAt()
}

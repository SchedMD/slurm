/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.util.Date;
import java.util.Map;
import java.util.Iterator;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.*;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import base.drawable.Shadow;
import base.drawable.NestingStacks;
import base.drawable.DrawnBoxSet;
import base.drawable.Method;
import base.statistics.BufForTimeAveBoxes;
import logformat.slog2.LineIDMap;
import logformat.slog2.input.TreeNode;
import logformat.slog2.input.TreeTrunk;
import viewer.common.Dialogs;
import viewer.common.Routines;
import viewer.common.Parameters;
import viewer.common.CustomCursor;
import viewer.zoomable.Debug;
import viewer.zoomable.Profile;
import viewer.zoomable.ModelTime;
import viewer.zoomable.YaxisMaps;
import viewer.zoomable.YaxisTree;
import viewer.zoomable.CoordPixelImage;
import viewer.zoomable.ScrollableObject;
import viewer.zoomable.SearchableView;
import viewer.zoomable.SummarizableView;
import viewer.zoomable.InfoDialog;
import viewer.zoomable.SearchPanel;
import viewer.zoomable.InitializableDialog;
import viewer.histogram.StatlineDialog;

public class CanvasTimeline extends ScrollableObject
                            implements SearchableView, SummarizableView
{
    private static final Drawable.Order INCRE_STARTTIME_ORDER
                                        = Drawable.INCRE_STARTTIME_ORDER;
    private static final Drawable.Order DECRE_STARTTIME_ORDER
                                        = Drawable.DECRE_STARTTIME_ORDER;
    private static final int            MIN_VISIBLE_ROW_COUNT = 2;
    private static       GradientPaint  BackgroundPaint       = null;

    private TreeTrunk          treetrunk;
    private YaxisMaps          y_maps;
    private YaxisTree          tree_view;
    private BoundedRangeModel  y_model;
    private Method[]           methods;
    private String[]           y_colnames;

    private Frame              root_frame;
    private TimeBoundingBox    timeframe4imgs;   // TimeFrame for images[]

    private ChangeListener     change_listener;
    private ChangeEvent        change_event;

    private int                num_rows;
    private int                row_height;
    private NestingStacks      nesting_stacks;
    private Map                map_line2row;
    private DrawnBoxSet        drawn_boxes;

    private boolean            isConnectedComposite;
    private SearchTreeTrunk    tree_search;

    private Date               zero_time, init_time, final_time;


    public CanvasTimeline( ModelTime           time_model,
                           TreeTrunk           treebody,
                           BoundedRangeModel   yaxis_model,
                           YaxisMaps           yaxis_maps,
                           String[]            yaxis_colnames,
                           Method[]            dobj_methods )
    {
        super( time_model );

        TreeNode   treeroot;
        short      depth_max, depth_init;

        treetrunk       = treebody;
        y_maps          = yaxis_maps;
        tree_view       = y_maps.getTreeView();
        y_model         = yaxis_model;
        y_colnames      = yaxis_colnames;
        methods         = dobj_methods;
        treeroot        = treetrunk.getTreeRoot();
        depth_max       = treeroot.getTreeNodeID().depth;
        nesting_stacks  = new NestingStacks( tree_view );
        map_line2row    = null;
        drawn_boxes     = new DrawnBoxSet( tree_view );
        // timeframe4imgs to be initialized later in initializeAllOffImages()
        timeframe4imgs  = null;

        depth_init      = (short) ( depth_max
                                  - Parameters.INIT_SLOG2_LEVEL_READ + 1 );
        if ( depth_init < 0 )
            depth_init = 0;
        treetrunk.growInTreeWindow( treeroot, depth_init,
                                    new TimeBoundingBox( treeroot ) );
        treetrunk.setNumOfViewsPerUpdate( ScrollableObject.NumViewsTotal );

        isConnectedComposite = false;
        if ( methods != null && methods.length > 0 )
            isConnectedComposite = methods[ 0 ].isConnectCompositeState();

        tree_search     = new SearchTreeTrunk( treetrunk, tree_view,
                                               isConnectedComposite );

        root_frame      = null;
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
            Debug.println( "CanvasTimeline: min_size = "
                         + "(0," + min_view_height + ")" );
        return new Dimension( 0, min_view_height );
    }

    public Dimension getMaximumSize()
    {
        if ( Debug.isActive() )
            Debug.println( "CanvasTimeline: max_size = "
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
        boolean isScrolling;

        if ( Profile.isActive() )
            zero_time = new Date();

        if ( root_frame == null )
            root_frame  = (Frame) SwingUtilities.windowForComponent( this );
        if ( timeframe4imgs == null )
            timeframe4imgs = new TimeBoundingBox( imgs_times );

        // Read the SLOG-2 TreeNodes within TimeFrame into memory
        /*
           The cursor needs to be set from the top container, so even when
           the mouse is at other components, e.g. ScrollbarTime,
           the cursor can still turn HourGlass.
        */
        Routines.setComponentAndChildrenCursors( root_frame,
                                                 CustomCursor.Wait );
        num_rows    = tree_view.getRowCount();
        row_height  = tree_view.getRowHeight();
        isScrolling = ( treetrunk.updateTimeWindow( timeframe4imgs, imgs_times )
                      == TreeTrunk.TIMEBOX_SCROLLING );
        nesting_stacks.initialize( isScrolling );

        if ( Profile.isActive() )
            init_time = new Date();

        map_line2row = y_maps.getMapOfLineIDToRowID();
        if ( map_line2row == null ) {
            if ( ! y_maps.update() )
                Dialogs.error( root_frame,
                               "Error in updating YaxisMaps!" );
            map_line2row = y_maps.getMapOfLineIDToRowID();
        }
        // System.out.println( "map_line2row = " + map_line2row );
        drawn_boxes.initialize();
    }

    protected void finalizeAllOffImages( final TimeBoundingBox imgs_times )
    {
        drawn_boxes.finish();
        map_line2row = null;
        nesting_stacks.finish();
        // Update the timeframe of all images
        timeframe4imgs.setEarliestTime( imgs_times.getEarliestTime() );
        timeframe4imgs.setLatestTime( imgs_times.getLatestTime() );
        this.fireChangeEvent();  // to update TreeTrunkPanel.
        Routines.setComponentAndChildrenCursors( root_frame,
                                                 CustomCursor.Normal );

        if ( Profile.isActive() )
            final_time = new Date();
        if ( Profile.isActive() )
            Profile.println( "CanvasTimeline.finalizeAllOffImages(): "
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
            Debug.println( "CanvasTimeline: drawOneOffImage()'s offImage = "
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
            // offGraphics.setPaint( BackgroundPaint );
            offGraphics.setPaint(
                        (Color) Parameters.BACKGROUND_COLOR.toValue() );
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

            nesting_stacks.reset();
            drawn_boxes.reset();


            Iterator sobjs;
            Shadow   sobj;
            Iterator dobjs;
            Drawable dobj;

            // set NestingFactor/RowID of Nestable Real Drawables and Shadows
            dobjs = treetrunk.iteratorOfAllDrawables( timebounds,
                                                      INCRE_STARTTIME_ORDER,
                                                      isConnectedComposite,
                                                      true );
            while ( dobjs.hasNext() ) {
                dobj = (Drawable) dobjs.next();
                if ( dobj.getCategory().isVisible() ) {
                    dobj.setStateRowAndNesting( coord_xform, map_line2row,
                                                nesting_stacks );
                }
            }

            int N_nestable = 0, N_nestless = 0;
            int N_nestable_drawn = 0, N_nestless_drawn = 0;
            
            // Draw Nestable Real Drawables
            dobjs = treetrunk.iteratorOfRealDrawables( timebounds,
                                                       INCRE_STARTTIME_ORDER,
                                                       isConnectedComposite,
                                                       true );
            while ( dobjs.hasNext() ) {
                dobj = (Drawable) dobjs.next();
                if ( dobj.getCategory().isVisible() ) {
                    N_nestable_drawn +=
                    dobj.drawOnCanvas( offGraphics, coord_xform,
                                       map_line2row, drawn_boxes );
                    N_nestable += dobj.getNumOfPrimitives();
                }
            }

            // Draw Nestable Shadows
            sobjs = treetrunk.iteratorOfLowestFloorShadows( timebounds,
                                                          INCRE_STARTTIME_ORDER,
                                                          true );
            while ( sobjs.hasNext() ) {
                sobj = (Shadow) sobjs.next();
                if ( sobj.getCategory().isVisible() ) {
                    N_nestable_drawn +=
                    sobj.drawOnCanvas( offGraphics, coord_xform,
                                       map_line2row, drawn_boxes );
                    N_nestable += sobj.getNumOfPrimitives();
                }
            }

            // Set AntiAliasing from Parameters for all slanted lines
            offGraphics.setRenderingHint( RenderingHints.KEY_ANTIALIASING,
                                    Parameters.ARROW_ANTIALIASING.toValue() );

            // Draw Nestless Shadows
            /*
            sobjs = treetrunk.iteratorOfLowestFloorShadows( timebounds,
                                                          INCRE_STARTTIME_ORDER,
                                                          false );
            while ( sobjs.hasNext() ) {
                sobj = (Shadow) sobjs.next();
                if ( sobj.getCategory().isVisible() ) {
                    N_nestless_drawn +=
                    sobj.drawOnCanvas( offGraphics, coord_xform,
                                       map_line2row, drawn_boxes );
                    N_nestless += sobj.getNumOfPrimitives();
                }
            }
            */

            // Draw all Nestless Real Drawables and Shadows
            dobjs = treetrunk.iteratorOfAllDrawables( timebounds,
                                                      INCRE_STARTTIME_ORDER,
                                                      isConnectedComposite,
                                                      false );
            while ( dobjs.hasNext() ) {
                dobj = (Drawable) dobjs.next(); 
                if ( dobj.getCategory().isVisible() ) {
                    N_nestless_drawn +=
                    dobj.drawOnCanvas( offGraphics, coord_xform,
                                       map_line2row, drawn_boxes );
                    N_nestless += dobj.getNumOfPrimitives();
                }
            }

            if ( Profile.isActive() )
                Profile.println( "CanvasTimeline.drawOneOffImage(): "
                               + "R_NestAble = "
                               + N_nestable_drawn + "/" + N_nestable + ",  "
                               + "R_NestLess = "
                               + N_nestless_drawn + "/" + N_nestless );

            // System.out.println( treetrunk.toStubString() );
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

        Map map_line2treeleaf = y_maps.getMapOfLineIDToTreeLeaf();

        Map map_line2row = y_maps.getMapOfLineIDToRowID();
        if ( map_line2row == null ) {
            if ( ! y_maps.update() )
                Dialogs.error( root_frame, "Error in updating YaxisMaps!" );
            map_line2row = y_maps.getMapOfLineIDToRowID();
        }

        Iterator sobjs;
        Shadow   sobj;
        Iterator dobjs;
        Drawable dobj;
        Drawable clicked_dobj;

        // Search Nestless Drawables in reverse drawing order
        dobjs = treetrunk.iteratorOfAllDrawables( vport_timeframe,
                                                  DECRE_STARTTIME_ORDER,
                                                  isConnectedComposite,
                                                  false );
        while ( dobjs.hasNext() ) {
            dobj = (Drawable) dobjs.next();
            if ( dobj.getCategory().isVisible() ) {
                clicked_dobj = dobj.getDrawableAt( coord_xform,
                                                   map_line2row,
                                                   local_click );
                if (    clicked_dobj != null
                     && clicked_dobj.getCategory().isVisible() ) {
                    return  new InfoDialogForDrawable( root_frame,
                                                       clicked_time,
                                                       map_line2treeleaf,
                                                       y_colnames,
                                                       clicked_dobj );
                }
            }
        }

        // Search Nestless Shadows in reverse drawing order
        /*
        sobjs = treetrunk.iteratorOfLowestFloorShadows( vport_timeframe,
                                                        DECRE_STARTTIME_ORDER,
                                                        false );
        while ( sobjs.hasNext() ) {
            sobj = (Shadow) sobjs.next();
            if ( sobj.getCategory().isVisible() ) {
                clicked_dobj = sobj.getDrawableAt( coord_xform,
                                                   map_line2row,
                                                   local_click );
                if (    clicked_dobj != null
                     && clicked_dobj.getCategory().isVisible() ) {
                    return  new InfoDialogForDrawable( root_frame,
                                                       clicked_time,
                                                       map_line2treeleaf,
                                                       y_colnames,
                                                       clicked_dobj );
                }
            }
        }
        */
        
        // Search Nestable Shadows in reverse drawing order
        sobjs = treetrunk.iteratorOfLowestFloorShadows( vport_timeframe,
                                                        DECRE_STARTTIME_ORDER,
                                                        true );
        while ( sobjs.hasNext() ) {
            sobj = (Shadow) sobjs.next();
            if ( sobj.getCategory().isVisible() ) {
                clicked_dobj = sobj.getDrawableAt( coord_xform,
                                                   map_line2row,
                                                   local_click );
                if (    clicked_dobj != null
                     && clicked_dobj.getCategory().isVisible() ) {
                    return  new InfoDialogForDrawable( root_frame,
                                                       clicked_time,
                                                       map_line2treeleaf,
                                                       y_colnames,
                                                       clicked_dobj );
                }
            }
        }

        // Search Nestable Drawables in reverse drawing order
        dobjs = treetrunk.iteratorOfRealDrawables( vport_timeframe,
                                                   DECRE_STARTTIME_ORDER,
                                                   isConnectedComposite,
                                                   true );
        while ( dobjs.hasNext() ) {
            dobj = (Drawable) dobjs.next();
            if ( dobj.getCategory().isVisible() ) {
                clicked_dobj = dobj.getDrawableAt( coord_xform,
                                                   map_line2row,
                                                   local_click );
                if (    clicked_dobj != null
                     && clicked_dobj.getCategory().isVisible() ) {
                    return  new InfoDialogForDrawable( root_frame,
                                                       clicked_time,
                                                       map_line2treeleaf,
                                                       y_colnames,
                                                       clicked_dobj );
                }
            }
        }

        return super.getTimePropertyAt( local_click );
    }   // endof  getPropertyAt()



    public Rectangle localRectangleForDrawable( final Drawable dobj )
    {
        CoordPixelImage       coord_xform;
        Rectangle             local_rect;
        int                   rowID;
        float                 nesting_ftr;
        float                 rStart, rFinal;
        int                   xloc, yloc, width, height;
        // local_rect is created with CanvasTimeline's pixel coordinate system
        coord_xform = new CoordPixelImage( this, row_height,
                                           super.getTimeBoundsOfImages() );
        xloc   = coord_xform.convertTimeToPixel( dobj.getEarliestTime() );
        width  = coord_xform.convertTimeToPixel( dobj.getLatestTime() )
               - xloc;

        /* assume RowID and NestingFactor have been calculated */
        rowID       = dobj.getRowID();
        nesting_ftr = dobj.getNestingFactor();
        rStart      = (float) rowID - nesting_ftr / 2.0f;
        rFinal      = rStart + nesting_ftr;

        yloc   = coord_xform.convertRowToPixel( rStart );
        height = coord_xform.convertRowToPixel( rFinal ) - yloc;
        local_rect = new Rectangle( xloc, yloc, width, height );
        return local_rect;
    }

    private InfoPanelForDrawable createInfoPanelForDrawable( Drawable dobj )
    {
        InfoPanelForDrawable  info_popup;

        Map map_line2treeleaf = y_maps.getMapOfLineIDToTreeLeaf();
        info_popup = new InfoPanelForDrawable( map_line2treeleaf,
                                               y_colnames, dobj );
        return info_popup;
    }

    // NEW search starting from the specified time
    public SearchPanel searchPreviousComponent( double searching_time )
    {
        Drawable  dobj = tree_search.previousDrawable( searching_time );
        if ( dobj != null )
            return this.createInfoPanelForDrawable( dobj );
        else
            return null;
    }

    // CONTINUING search
    public SearchPanel searchPreviousComponent()
    {
        Drawable  dobj = tree_search.previousDrawable();
        if ( dobj != null )
            return this.createInfoPanelForDrawable( dobj );
        else
            return null;
    }

    // NEW search starting from the specified time
    public SearchPanel searchNextComponent( double searching_time )
    {
        Drawable  dobj = tree_search.nextDrawable( searching_time );
        if ( dobj != null )
            return this.createInfoPanelForDrawable( dobj );
        else
            return null;
    }

    // CONTINUING search
    public SearchPanel searchNextComponent()
    {
        Drawable  dobj = tree_search.nextDrawable();
        if ( dobj != null )
            return this.createInfoPanelForDrawable( dobj );
        else
            return null;
    }

    // Interface for SummarizableView
    public InitializableDialog createSummary( final Dialog          dialog,
                                              final TimeBoundingBox timebox )
    {
        BufForTimeAveBoxes  buf4statboxes;

        buf4statboxes  = tree_search.createBufForTimeAveBoxes( timebox );
        // System.out.println( "Statistics = " + buf4statboxes );
        return new StatlineDialog( dialog, timebox,
                                   y_maps.getLineIDMap(), buf4statboxes );
    }
}

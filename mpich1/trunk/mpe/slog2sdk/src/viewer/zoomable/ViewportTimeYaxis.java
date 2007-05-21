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
import javax.swing.event.*;

import base.drawable.Drawable;
import viewer.common.Dialogs;
import viewer.common.Parameters;

public class ViewportTimeYaxis extends ViewportTime
                               implements AdjustmentListener
{
    private static final Color    SEARCH_LINE_COLOR       = Color.yellow;
    private static final int      SEARCH_ARROW_HALF_ANGLE = 15;          // deg
    private static final double   SEARCH_ARROW_ANGLE      = Math.PI/6.0; // rad
    private static final double   COS_SEARCH_ARROW_ANGLE
                                  = Math.cos( SEARCH_ARROW_ANGLE );
    private static final double   SIN_SEARCH_ARROW_ANGLE
                                  = Math.sin( SEARCH_ARROW_ANGLE );

    private ModelTime             time_model      = null;
    private BoundedRangeModel     y_model         = null;
    private YaxisTree             tree_view       = null;

    private Point                 view_pt         = null;
    private ComponentEvent        resize_evt      = null;

    // searchable = view_img is both a Component and ScrollableView object
    private SearchableView        searchable      = null;
    private SearchDialog          search_dialog   = null;

    private Drawable              searched_dobj   = null;
    private double                searching_time;              

    public ViewportTimeYaxis( final ModelTime time_axis_model, 
                              BoundedRangeModel yaxis_model, YaxisTree y_tree )
    {
        super( time_axis_model );
        time_model  = time_axis_model;
        y_model     = yaxis_model;
        tree_view   = y_tree;
        view_pt     = new Point( 0, 0 );
        resize_evt  = new ComponentEvent( this,
                                          ComponentEvent.COMPONENT_RESIZED );
        searching_time = time_model.getTimeGlobalMinimum();
    }

    public void setView( Component view )
    {
        super.setView( view );
        if ( view instanceof SearchableView )
            searchable  = (SearchableView) view;
        else
            // causes exception if SearchableView interface is invoked
            searchable  = null;
    }

    public void adjustmentValueChanged( AdjustmentEvent evt )
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTimeYaxis: adjChanged()'s START: " );
            Debug.println( "adj_evt = " + evt );
        }
        view_pt.x  = super.getXaxisViewPosition();
        view_pt.y  = evt.getValue();
        super.setYaxisViewPosition( view_pt.y );
        super.setViewPosition( view_pt );
            /*
               calling view.repaint() to ensure the view is repainted
               after setViewPosition is called.
               -- apparently, super.repaint(), the RepaintManager, has invoked 
                  ( (Component) view_img ).repaint();
               -- JViewport.setViewPosition() may have invoked super.repaint()

               calling the ViewortTime.paint() to avoid redrawing in this class
            */
        super.repaint();
        if ( Debug.isActive() )
            Debug.println( "ViewportTimeYaxis: adjChanged()'s END: " );
    }

    public void fireComponentResized()
    {
        super.componentResized( resize_evt );
    }


    private Rectangle  localRectangleForDrawable( final Drawable dobj )
    {
        Rectangle  dobj_rect;
        Point      local_click; 
        /*
           SwingUtilities.convertPoint() has to be called after the
           CanvasXXXXline has been scrolled, i.e. time_model.scroll().
        */
        dobj_rect = searchable.localRectangleForDrawable( dobj );
        // Update dobj's location to be in ViewportTimeYaxis's coordinate system
        local_click = SwingUtilities.convertPoint( (Component) searchable,
                                                   dobj_rect.x, dobj_rect.y,
                                                   this );
        dobj_rect.setLocation( local_click );
        return dobj_rect;
    }

    private void drawMarkerForSearchedDrawable( Graphics g )
    {
        Stroke     orig_stroke;
        Rectangle  dobj_rect;
        Color      dobj_color, dobj_brighter_color, dobj_darker_color;
        int        vport_width, vport_height;
        int        radius, diameter;
        int        arrow_Xoff, arrow_Yoff;
        int        frame_thickness;
        int        x1, y1, x2, y2, ii;

        vport_width     = this.getWidth();
        vport_height    = this.getHeight();
        dobj_rect       = this.localRectangleForDrawable( searched_dobj );
        // Draw a vertical line along the searched_time;
        if ( dobj_rect.x >= 0 && dobj_rect.x < vport_width ) {
            dobj_color           = searched_dobj.getCategory().getColor();
            dobj_brighter_color  = dobj_color.brighter();
            dobj_darker_color    = dobj_color.darker();

            frame_thickness = Parameters.SEARCH_FRAME_THICKNESS;
            radius          = Parameters.SEARCH_ARROW_LENGTH;
            diameter        = 2 * radius;
            arrow_Xoff      = (int) (radius*SIN_SEARCH_ARROW_ANGLE + 0.5d);
            arrow_Yoff      = (int) (radius*COS_SEARCH_ARROW_ANGLE + 0.5d);

            // Fill upper arrowhead with 2 shades of color
            x1 = dobj_rect.x;
            y1 = dobj_rect.y - frame_thickness;
            g.setColor( dobj_color ); // g.setColor( Color.yellow );
            g.fillArc( x1-radius, y1-radius, diameter, diameter,
                       90, -SEARCH_ARROW_HALF_ANGLE );
            g.setColor( dobj_darker_color ); // g.setColor( Color.gray );
            g.fillArc( x1-radius, y1-radius, diameter, diameter,
                       90-SEARCH_ARROW_HALF_ANGLE, -SEARCH_ARROW_HALF_ANGLE );
            // Draw upper arrowhead with border
            g.setColor( dobj_brighter_color ); // g.setColor( Color.gray );
            g.drawLine( x1, y1, x1, y1-radius );
            g.setColor( dobj_brighter_color ); // g.setColor( Color.white );
            g.drawLine( x1, y1, x1+arrow_Xoff, y1-arrow_Yoff );

            // Fill lower arrowhead with 2 shades of color
            x2 = x1;
            y2 = dobj_rect.y + dobj_rect.height + frame_thickness;
            g.setColor( Color.yellow );
            g.fillArc( x2-radius, y2-radius, diameter, diameter,
                       270, SEARCH_ARROW_HALF_ANGLE );
            g.setColor( Color.darkGray );
            g.fillArc( x2-radius, y2-radius, diameter, diameter,
                       270+SEARCH_ARROW_HALF_ANGLE, SEARCH_ARROW_HALF_ANGLE );
            // Draw lower arrowhead with border
            g.setColor( Color.gray );
            g.drawLine( x2, y2, x2, y2+radius );
            g.setColor( Color.white );
            g.drawLine( x2, y2, x2+arrow_Xoff, y2+arrow_Yoff );
        }

        // Compute the intersecting rectangle % the Viewport & the drawable
        dobj_rect = SwingUtilities.computeIntersection(
                                   0, 0, vport_width, vport_height, dobj_rect );
        if ( dobj_rect.x >= 0 ) {
            if ( Parameters.SEARCHED_OBJECT_ON_TOP ) {
                g.setColor( searched_dobj.getCategory().getColor() );
                g.fillRect( dobj_rect.x, dobj_rect.y,
                            dobj_rect.width, dobj_rect.height );
            }
            frame_thickness = Parameters.SEARCH_FRAME_THICKNESS;
            x1  = dobj_rect.x;
            y1  = dobj_rect.y;
            x2  = x1 + dobj_rect.width;
            y2  = y1 + dobj_rect.height;
            // Draw the innermost left & top with a dark color
            g.setColor( Color.black );
                ii = 0;
                g.drawLine( x1-ii, y1-ii, x1-ii, y2+ii );  // left
                g.drawLine( x1-ii, y1-ii, x2+ii, y1-ii );  // top
            // Draw left & top with a bright color
            g.setColor( Color.white );
            for ( ii = 1; ii <= frame_thickness; ii++ ) {
                g.drawLine( x1-ii, y1-ii, x1-ii, y2+ii );  // left
                g.drawLine( x1-ii, y1-ii, x2+ii, y1-ii );  // top
            }
            // Draw the innermost right & bottom with a bright color
            g.setColor( Color.white );
                ii = 0;
                g.drawLine( x2+ii, y1-ii, x2+ii, y2+ii );  // right
                g.drawLine( x1-ii, y2+ii, x2+ii, y2+ii );  // bottom
            // Draw right & bottom with a dark color
            g.setColor( Color.darkGray );
            for ( ii = 1; ii <= frame_thickness; ii++ ) {
                g.drawLine( x2+ii, y1-ii, x2+ii, y2+ii );  // right
                g.drawLine( x1-ii, y2+ii, x2+ii, y2+ii );  // bottom
            }
        }
    }

    public void paint( Graphics g )
    {
        int   x_pos;

        if ( Debug.isActive() )
            Debug.println( "ViewportTimeYaxis: paint()'s START: " );

        super.paint( g );

        // Draw a line at searching_time
        if ( super.coord_xform.contains( searching_time ) ) {
            x_pos = super.coord_xform.convertTimeToPixel( searching_time );
            g.setColor( SEARCH_LINE_COLOR );
            g.drawLine( x_pos, 0, x_pos, this.getHeight() );
        }
        // Draw marker around searched_dobj if it exists
        if ( searched_dobj != null )
            this.drawMarkerForSearchedDrawable( g );

        if ( Debug.isActive() )
            Debug.println( "ViewportTimeYaxis: paint()'s END: " );
    }

    public void eraseSearchedDrawable()
    {
        searched_dobj = null;
    }


    private static final double INVALID_TIME   = Double.NEGATIVE_INFINITY;
    private              double searched_time  = INVALID_TIME;

    /*
        searchBackward() is for ActionSearchBackward
    */
    public boolean searchBackward()
    {
        SearchPanel  dobj_panel = null;

        // searchBackward can only be called from TimelineFrame, JFrame.
        if ( search_dialog == null ) {
            Window window  = SwingUtilities.windowForComponent( this );
            if ( window instanceof Frame )
                search_dialog  = new SearchDialog( (Frame) window, this );
            else
                Dialogs.error( window,
                               "ViewportTimeYaxis.searchBackward() "
                             + "is meant to be invoked from a top JFrame." );
        }
        
        if ( searching_time != searched_time )
            dobj_panel  = searchable.searchPreviousComponent( searching_time );
        else
            dobj_panel  = searchable.searchPreviousComponent();
        if ( dobj_panel != null ) {
            searched_dobj = dobj_panel.getSearchedDrawable();
            searched_time = searched_dobj.getEarliestTime();
            // Scroll the Time axis and set Time Focus at the drawable found.
            time_model.scroll( searched_time - searching_time );
            searching_time = searched_time;
            // Scroll the Y-axis as well so searched_dobj becomes visible
            tree_view.scrollRowToVisible( searched_dobj.getRowID() );
            //  call this.paint( g );
            this.repaint();

            search_dialog.replace( dobj_panel );
            if ( ! search_dialog.isVisible() )
                 search_dialog.setVisibleAtDefaultLocation();
            return true;
        }
        else {
            if (    searched_dobj != null
                 && (    searched_dobj.getEarliestTime()
                      == time_model.getTimeGlobalMinimum() ) )
                Dialogs.info( SwingUtilities.windowForComponent( this ),
                              "The FIRST drawable in the logfile has been "
                            + "reached.\n  Search backward has no more "
                            + "drawable to return.\n", null );
            else
                Dialogs.warn( SwingUtilities.windowForComponent( this ),
                              "If the logfile's beginning is not within view,\n"
                            + "SCROLL BACKWARD till you see more drawables\n"
                            + "are within view.  All drawables in view or in \n"
                            + "the memory have been searched.\n" );
            search_dialog.setVisible( false );
            searched_dobj = null;
            searched_time = INVALID_TIME;
            this.repaint();
            return false;
        }
    }

    /*
        searchForward() is for ActionSearchForward
    */
    public boolean searchForward()
    {
        SearchPanel  dobj_panel = null;

        // searchForward can only be called from TimelineFrame, JFrame.
        if ( search_dialog == null ) {
            Window window  = SwingUtilities.windowForComponent( this );
            if ( window instanceof Frame )
                search_dialog  = new SearchDialog( (Frame) window, this );
            else
                Dialogs.error( window,
                               "ViewportTimeYaxis.searchForward() "
                             + "is meant to be invoked from a top JFrame." );
        }

        if ( searching_time != searched_time )
            dobj_panel  = searchable.searchNextComponent( searching_time );
        else
            dobj_panel  = searchable.searchNextComponent();

        if ( dobj_panel != null ) {
            searched_dobj = dobj_panel.getSearchedDrawable();
            searched_time = searched_dobj.getEarliestTime();
            // Scroll the screen and set Time Focus at the drawable found.
            time_model.scroll( searched_time - searching_time );
            searching_time = searched_time;
            // Scroll the Y-axis as well so searched_dobj becomes visible
            tree_view.scrollRowToVisible( searched_dobj.getRowID() );
            //  call this.paint( g );
            this.repaint();

            search_dialog.replace( dobj_panel );
            if ( ! search_dialog.isVisible() )
                 search_dialog.setVisibleAtDefaultLocation();
            return true;
        }
        else {
            if (    searched_dobj != null
                 && (    searched_dobj.getLatestTime()
                      == time_model.getTimeGlobalMaximum() ) )
                Dialogs.info( SwingUtilities.windowForComponent( this ),
                              "The LAST drawable in the logfile has been "
                            + "reached.\n  Search forward has no more "
                            + "drawable to return.\n", null );
            else
                Dialogs.warn( SwingUtilities.windowForComponent( this ),
                              "If the end of the logfile is not within view,\n"
                            + "SCROLL FORWARD till you see more drawables\n"
                            + "are within view.  All drawables in view or in \n"
                            + "the memory have been searched.\n" );
            search_dialog.setVisible( false );
            searched_dobj = null;
            searched_time = INVALID_TIME;
            this.repaint();
            return false;
        }
    }

    public boolean searchInit()
    {
        InfoDialog  info_popup = super.getLastInfoDialog();
        if ( info_popup != null ) {
            searching_time = info_popup.getClickedTime();
            info_popup.getCloseButton().doClick();
            this.repaint();
            return true;
        }
        else {
            Dialogs.warn( SwingUtilities.windowForComponent( this ),
                          "No info dialog box! Info dialog box can be set\n"
                        + "by right mouse clicking on the timeline canvas\n" );
            return false;
        }
    }



        /*
            Interface to Overload MouseInputListener()
        */
        public void mouseClicked( MouseEvent mouse_evt )
        {
            Point  vport_click;

            super.mouseClicked( mouse_evt );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( ! super.isLeftMouseClick4Zoom ) {  // Hand Mode
                    vport_click    = mouse_evt.getPoint();
                    searching_time = super.coord_xform.convertPixelToTime(
                                                       vport_click.x );
                    this.repaint();
                }
            }
        }

        private int     mouse_last_Yloc;
        private double  ratio_ymodel2vportH;

        /*
            In order to allow grasp & scroll along Y-axis, the change in
            mouse movement in Y-axis on this Viewport needs to be translated
            to movement in Yaxis scrollbar's model coordinate.  The trick is
            that the "extent" of Yaxis scrollbar's model should be mapped
            to the viewport height in pixel.
        */
        public void mousePressed( MouseEvent mouse_evt )
        {
            Point  vport_click;

            super.mousePressed( mouse_evt );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( ! super.isLeftMouseClick4Zoom ) {  // Hand Mode
                    vport_click          = mouse_evt.getPoint();
                    mouse_last_Yloc      = vport_click.y;
                    ratio_ymodel2vportH  = (double) y_model.getExtent()
                                                  / this.getHeight();
                }
            }
        }

        public void mouseDragged( MouseEvent mouse_evt )
        {
            Point  vport_click;
            int    y_change, sb_change;

            super.mouseDragged( mouse_evt );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( ! super.isLeftMouseClick4Zoom ) {  // Hand Mode
                    vport_click = mouse_evt.getPoint();
                    y_change    = mouse_last_Yloc - vport_click.y; 
                    sb_change   = (int) Math.round( ratio_ymodel2vportH
                                                  * y_change );
                    // y_model.setValue() invokes adjustmentValueChanged() above
                    y_model.setValue( y_model.getValue() + sb_change );
                    mouse_last_Yloc = vport_click.y;
                }
            }
        }

        public void mouseReleased( MouseEvent mouse_evt )
        {
            Point  vport_click;
            int    y_change, sb_change;

            super.mouseReleased( mouse_evt );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( ! super.isLeftMouseClick4Zoom ) {
                    vport_click = mouse_evt.getPoint();
                    y_change  = mouse_last_Yloc - vport_click.y; 
                    sb_change = (int) Math.round( ratio_ymodel2vportH
                                                * y_change );
                    // y_model.setValue() invokes adjustmentValueChanged() above
                    y_model.setValue( y_model.getValue() + sb_change );
                    mouse_last_Yloc = vport_click.y;
                }
            }
        }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Stack;
import java.net.URL;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.*;

import base.drawable.TimeBoundingBox;
import viewer.common.Const;
import viewer.common.CustomCursor;
import viewer.common.Routines;
import viewer.common.Parameters;

public class ViewportTime extends JViewport
                          implements TimeListener,
                                     ComponentListener,
                                     MouseInputListener,
                                     KeyListener
//                                     HierarchyBoundsListener
{
    private static final Color   INFO_LINE_COLOR  = Color.green;
    private static final Color   INFO_AREA_COLOR  = new Color(255,255,  0,64);
    private static final Color   ZOOM_LINE_COLOR  = Color.white;
    private static final Color   ZOOM_AREA_COLOR  = new Color(132,112,255,96);
    private static final Color   FOCUS_LINE_COLOR = Color.red;

    private   Point                     view_pt;
    // view_img is both a Component and ScrollableView object
    private   ScrollableView            view_img      = null;
    private   ModelTime                 time_model    = null;
    private   ToolBarStatus             toolbar       = null;

    private   TimeBoundingBox           vport_timebox = null;
    protected CoordPixelImage           coord_xform   = null;

    private   TimeBoundingBox           zoom_timebox  = null;
    private   TimeBoundingBox           info_timebox  = null;

    // info_dialogs list is used to keep track of all InfoDialog boxes.
    private   List                      info_dialogs;

    private   InfoDialogActionListener  info_action_listener;
    private   InfoDialogWindowListener  info_window_listener;

    protected boolean                   isLeftMouseClick4Zoom;
    private   JRadioButton              zoom_btn;
    private   JRadioButton              hand_btn;


    public ViewportTime( final ModelTime in_model )
    {
        time_model             = in_model;
        view_pt                = new Point( 0, 0 );
        isLeftMouseClick4Zoom  = false;   // default to Scroll with left mouse
        zoom_btn               = null;
        hand_btn               = null;
        /*
            For resizing of the viewport => resizing of the ScrollableView
        */
        addComponentListener( this );
        /*
            HierarchyBoundsListener is for the case when this class
            is moved but NOT resized.  That it checks for situation
            to reinitialize the size of ScrollableView when the 
            scrollable image's size is reset for some obscure reason.

            However, defining getPreferredSize() of ScrollableView
            seems to make HierarchyBoundsListener of this class
            unnecessary.
        */
        // addHierarchyBoundsListener( this );

        // setDebugGraphicsOptions( DebugGraphics.LOG_OPTION );
        vport_timebox       = new TimeBoundingBox();
    }

    public void setView( Component view )
    {
        super.setView( view );
        // Assume "view" has implemented the ComponentListener interface
        Dimension min_sz = view.getMinimumSize();
        if ( min_sz != null )
            setMinimumSize( min_sz );
        Dimension max_sz = view.getMaximumSize();
        if ( max_sz != null )
            setMaximumSize( max_sz );
        Dimension pref_sz = view.getPreferredSize();
        if ( pref_sz != null )
            setPreferredSize( pref_sz );
        view_img     = (ScrollableView) view;

        coord_xform  = new CoordPixelImage( (ScrollableObject) view_img );
        super.addMouseListener( this );
        super.addMouseMotionListener( this );
        super.addKeyListener( this );

        info_dialogs = new ArrayList();
        info_action_listener = new InfoDialogActionListener( this,
                                                             info_dialogs );
        info_window_listener = new InfoDialogWindowListener( this,
                                                             info_dialogs );
    }

    public void setToolBarStatus( ToolBarStatus  in_toolbar )
    {
        toolbar = in_toolbar;
    }

    //  For Debugging Profiling
    public Dimension getMinimumSize()
    {
        Dimension min_sz = super.getMinimumSize();
        if ( Debug.isActive() )
            Debug.println( "ViewportTime: min_size = " + min_sz );
        return min_sz;
    }

    //  For Debugging Profiling
    public Dimension getMaximumSize()
    {
        Dimension max_sz = super.getMaximumSize();
        if ( Debug.isActive() )
            Debug.println( "ViewportTime: max_size = " + max_sz );
        return max_sz;
    }

    //  For Debugging Profiling
    public Dimension getPreferredSize()
    {
        Dimension pref_sz = super.getPreferredSize();
        if ( Debug.isActive() )
            Debug.println( "ViewportTime: pref_size = " + pref_sz );
        return pref_sz;
    }

    protected void setYaxisViewPosition( int new_y_view_pos )
    {
        view_pt.y   = new_y_view_pos;
    }

    protected int  getXaxisViewPosition()
    {
        return view_pt.x;
    }

    /*
        timeChanged() is invoked by ModelTime's fireTimeChanged();

        Since ModelTime is the Model for the scroll_bar, timeChanged()
        will be called everytime when scroll_bar is moved/changed. 
    */
    public void timeChanged( TimeEvent evt )
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: timeChanged()'s START: " );
            Debug.println( "time_evt = " + evt );
        }
        if ( view_img != null ) {
            // view_img.checkToXXXXView() assumes constant image size
            view_img.checkToZoomView();
            view_img.checkToScrollView();
            if ( Debug.isActive() )
                Debug.println( "ViewportTime:timeChanged()'s view_img = "
                             + view_img );
            view_pt.x = view_img.getXaxisViewPosition();
            super.setViewPosition( view_pt );
            /*
               calling view.repaint() to ensure the view is repainted
               after setViewPosition is called.
               -- apparently, super.repaint(), the RepaintManager, has invoked 
                  ( (Component) view_img ).repaint();
               -- JViewport.setViewPosition() may have invoked super.repaint()
            */
            this.repaint();
        }
        if ( Debug.isActive() ) {
            if ( view_img != null ) {
                Debug.println( "ViewportTime: "
                             + "view_img.getXaxisViewPosition() = "
                             + view_pt.x );
                Debug.println( "ViewportTime: [after] getViewPosition() = "
                             + super.getViewPosition() );
            }
            Debug.println( "ViewportTime: timeChanged()'s END: " );
        }
    }

    public void componentResized( ComponentEvent evt )
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: componentResized()'s START: " );
            Debug.println( "comp_evt = " + evt );
        }
        if ( view_img != null ) {
            /*
               Instead of informing the view by ComponentEvent, i.e.
               doing addComponentListener( (ComponentListener) view ),
               ( (ComponentListener) view ).componentResized() is called
               directly here to ensure that view is resized before 
               super.setViewPosition() is called on view.  This is done
               to ensure the correct sequence of componentResized().
               This also means the "view" does NOT need to implement
               ComponentListener interface.
            */
            view_img.componentResized( this );
            /*
               It is very IMPORTANT to do setPreferredSize() for JViewport
               with custom JComponent view.  If PreferredSize is NOT set,
               the top-level container, JFrame, will have difficulty to
               compute the size final display window when calling
               Window.pack().  The consequence will be the initial
               view of JViewport has its getViewPosition() set to (0,0)
               in view coordinates during program starts up.
               Apparently, Window.pack() uses PreferredSize to compute
               window size.
            */
            this.setPreferredSize( getSize() );
            if ( Debug.isActive() )
                Debug.println( "ViewportTime: componentResized()'s view_img = "
                             + view_img );
            view_pt.x = view_img.getXaxisViewPosition();
            super.setViewPosition( view_pt );
            /*
               calling view.repaint() to ensure the view is repainted
               after setViewPosition is called.
               -- apparently, this.repaint(), the RepaintManager, has invoked 
                  ( (Component) view_img ).repaint();
               -- JViewport.setViewPosition() may have invoked super.repaint()
            */
            this.repaint();
        }
        if ( Debug.isActive() ) {
            if ( view_img != null ) {
                Debug.println( "ViewportTime: "
                             + "view_img.getXaxisViewPosition() = "
                             + view_pt.x );
                Debug.println( "ViewportTime: [after] getViewPosition() = "
                             + super.getViewPosition() );
            }
            Debug.println( "ViewportTime: componentResized()'s END: " );
        }
    }


    public void componentMoved( ComponentEvent evt ) 
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: componentMoved()'s START: " );
            Debug.println( "comp_evt = " + evt );
            Debug.println( "ViewportTime: componentMoved()'s END: " );
        }
    }

    public void componentHidden( ComponentEvent evt ) 
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: componentHidden()'s START: " );
            Debug.println( "comp_evt = " + evt );
            Debug.println( "ViewportTime: componentHidden()'s END: " );
        }
    }

    public void componentShown( ComponentEvent evt ) 
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: componentShown()'s START: " );
            Debug.println( "comp_evt = " + evt );
            Debug.println( "ViewportTime: componentShown()'s END: " );
        }
    }

    private void drawShadyTimeBoundingBox( Graphics g,
                                           final TimeBoundingBox timebox,
                                           Color line_color, Color area_color )
    {
        double      line_time;
        int         x1_pos, x2_pos;

        if ( vport_timebox.overlaps( timebox ) ) {
            line_time = timebox.getEarliestTime();
            if ( coord_xform.contains( line_time ) ) {
                x1_pos = coord_xform.convertTimeToPixel( line_time );
                g.setColor( line_color );
                g.drawLine( x1_pos, 0, x1_pos, this.getHeight() );
            }
            else
                x1_pos = 0;

            line_time = timebox.getLatestTime();
            if ( coord_xform.contains( line_time ) ) {
                x2_pos = coord_xform.convertTimeToPixel( line_time );
                g.setColor( line_color );
                g.drawLine( x2_pos, 0, x2_pos, this.getHeight() );
            }
            else
                x2_pos = this.getWidth();

            if ( x2_pos > x1_pos ) {
                g.setColor( area_color );
                g.fillRect( x1_pos+1, 0, x2_pos-x1_pos-1, this.getHeight() );
            }
        }
    }

    public void paint( Graphics g )
    {
        Iterator    itr;
        InfoDialog  info_popup;
        double      popup_time;
        double      focus_time;
        int         x_pos;

        if ( Debug.isActive() )
            Debug.println( "ViewportTime: paint()'s START: " );

        // Need to get the FOCUS so KeyListener will respond.
        // requestFocus();

        //  "( (Component) view_img ).repaint()" may have been invoked
        //  in JComponent's paint() method's paintChildren() ?!
        super.paint( g );

        /*  Initialization  */
        vport_timebox.setEarliestTime( time_model.getTimeViewPosition() );
        vport_timebox.setLatestFromEarliest( time_model.getTimeViewExtent() );
        coord_xform.resetTimeBounds( vport_timebox );

        // Draw a line at time_model.getTimeZoomFocus() 
        if ( ! Parameters.LEFTCLICK_INSTANT_ZOOM ) {
            focus_time = time_model.getTimeZoomFocus();
            if ( coord_xform.contains( focus_time ) ) {
                x_pos = coord_xform.convertTimeToPixel( focus_time );
                g.setColor( FOCUS_LINE_COLOR );
                g.drawLine( x_pos, 0, x_pos, this.getHeight() );
            }
        }

        /*  Draw zoom boundary  */
        if ( zoom_timebox != null )
            this.drawShadyTimeBoundingBox( g, zoom_timebox,
                                           ZOOM_LINE_COLOR,
                                           ZOOM_AREA_COLOR );

        if ( info_timebox != null )
            this.drawShadyTimeBoundingBox( g, info_timebox,
                                           INFO_LINE_COLOR,
                                           INFO_AREA_COLOR );

        /*  Draw the InfoDialog marker  */
        itr = info_dialogs.iterator();
        while ( itr.hasNext() ) {
            info_popup = (InfoDialog) itr.next();
            if ( info_popup instanceof InfoDialogForDuration ) {
                InfoDialogForDuration  popup;
                popup = (InfoDialogForDuration) info_popup;
                this.drawShadyTimeBoundingBox( g, popup.getTimeBoundingBox(),
                                               INFO_LINE_COLOR,
                                               INFO_AREA_COLOR );
            }
            else {
                popup_time = info_popup.getClickedTime();
                if ( coord_xform.contains( popup_time ) ) {
                    x_pos = coord_xform.convertTimeToPixel( popup_time );
                    g.setColor( INFO_LINE_COLOR );
                    g.drawLine( x_pos, 0, x_pos, this.getHeight() );
                }
            }
        }

        if ( Debug.isActive() )
            Debug.println( "ViewportTime: paint()'s END: " );
    }


    /*
        Implementation of HierarchyBoundsListener
    */
/*
    public void ancestorMoved( HierarchyEvent evt )
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: ancestorMoved()'s START: " );
            Debug.println( "hrk_evt = " + evt );
            Debug.println( "ViewportTime: ancestorMoved()'s END: " );
        }
    }

    public void ancestorResized( HierarchyEvent evt )
    {
        if ( Debug.isActive() ) {
            Debug.println( "ViewportTime: ancestorResized()'s START: " );
            Debug.println( "hrk_evt = " + evt );
            if ( view_img != null ) {
                Debug.println( "ViewportTime: "
                             + "view_img.getXaxisViewPosition() = "
                             + view_pt.x );
                Debug.println( "ViewportTime: [before] getViewPosition() = "
                             + super.getViewPosition() );
                Debug.println( "ViewportTime: ancestorMoved()'s this = "
                             + this );
            }
        }
        if ( view_img != null ) {
            //  ScrollableView.setJComponentSize(),
            //  JViewport.setPreferredSize() and JViewport.setViewPosition()
            //  need to be called when the topmost container in the
            //  containment hierarchy is
            //  resized but this class is moved but NOT resized.  In 
            //  this scenario, the resizing of topmost container seems 
            //  to reset the location of scrollable to (0,0) as well as 
            //  the size of the ScrollableView to the visible size of 
            //  the JViewport.

            //  view_img.setJComponentSize();
            this.setPreferredSize( getSize() );
            view_pt.x = view_img.getXaxisViewPosition();
            super.setViewPosition( view_pt );

            // calling view.repaint() to ensure the view is repainted
            // after setViewPosition is called.
            // -- apparently, this.repaint(), the RepaintManager, has invoked 
            //    ( (Component) view_img ).repaint();
            // -- JViewport.setViewPosition() may have invoked super.repaint()

            this.repaint();
        }
        if ( Debug.isActive() ) {
            if ( view_img != null ) {
                Debug.println( "ViewportTime: "
                             + "view_img.getXaxisViewPosition() = "
                             + view_pt.x );
                Debug.println( "ViewportTime: [after] getViewPosition() = "
                             + super.getViewPosition() );
                Debug.println( "ViewportTime: ancestorMoved()'s this = "
                             + this );
            }
            Debug.println( "ViewportTime: ancestorResized()'s END: " );
        }
    }
*/

    private URL getURL( String filename )
    {
        return getClass().getResource( filename );
    }

    public void initLeftMouseToZoom( boolean in_isLeftMouseClick4Zoom )
    {
        ButtonGroup  btn_group;
        ImageIcon    icon, icon_shaded;
        URL          icon_URL;

        isLeftMouseClick4Zoom  = in_isLeftMouseClick4Zoom;
        btn_group              = new ButtonGroup();

        icon_URL  = getURL( Const.IMG_PATH + "ZoomBW16.gif" );
        if ( icon_URL != null ) {
            icon         = new ImageIcon( icon_URL );
            icon_shaded  = new ImageIcon(
                           GrayFilter.createDisabledImage( icon.getImage() ) );
            /*
            System.out.println( "ZoomBW16.gif: icon = (" + icon.getIconWidth()
                              + "," + icon.getIconHeight() + "),  "
                              + "icon_shaded = (" + icon_shaded.getIconWidth()
                              + "," + icon_shaded.getIconHeight() + ")" );
            */
            zoom_btn     = new JRadioButton( icon_shaded );
            zoom_btn.setSelectedIcon( icon );
            zoom_btn.setBorderPainted( true );
        }
        else
            zoom_btn = new JRadioButton( "zoom" );
        zoom_btn.setToolTipText( "Left mouse button click to Zoom" );
        zoom_btn.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt )
            {
                if ( zoom_btn.isSelected() )
                    isLeftMouseClick4Zoom = true;
            }
        } );
        if ( isLeftMouseClick4Zoom )
            zoom_btn.doClick();
        btn_group.add( zoom_btn );

        icon_URL  = getURL( Const.IMG_PATH + "HandOpen16.gif" );
        if ( icon_URL != null ) {
            icon         = new ImageIcon( icon_URL );
            icon_shaded  = new ImageIcon(
                           GrayFilter.createDisabledImage( icon.getImage() ) );
            hand_btn = new JRadioButton( icon_shaded );
            hand_btn.setSelectedIcon( icon );
            hand_btn.setBorderPainted( true );
        }
        else
            hand_btn = new JRadioButton( "hand" );
        hand_btn.setToolTipText( "Left mouse button click to Scroll" );
        hand_btn.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt )
            {
                if ( hand_btn.isSelected() )
                    isLeftMouseClick4Zoom = false;
            }
        } );
        if ( !isLeftMouseClick4Zoom )
            hand_btn.doClick();
        btn_group.add( hand_btn );
    }

    public JPanel createLeftMouseModePanel( int boxlayout_mode )
    {
        JPanel   btn_panel = null;
        btn_panel = null;
        if ( zoom_btn != null && hand_btn != null ) {
            btn_panel = new JPanel();
            btn_panel.setLayout( new BoxLayout( btn_panel, boxlayout_mode ) );
            btn_panel.add( zoom_btn );
            btn_panel.add( hand_btn );
            btn_panel.setBorder( BorderFactory.createEtchedBorder() );
        }
        return btn_panel;
    }


        // Override the Component.setCursor()
        public void setCursor( Cursor new_cursor )
        {
            /*
               Replace the DEFAULT_CURSOR by ZoomPlus or HandOpen cursor.
               i.e. the default cursor for this class is either 
               ZoomPlus or HandOpen cursor depending on isLeftMouseClick4Zoom.
            */
            if ( new_cursor == CustomCursor.Normal ) {
                if ( isLeftMouseClick4Zoom )
                    super.setCursor( CustomCursor.ZoomPlus );
                else
                    super.setCursor( CustomCursor.HandOpen );
            }
            else
                super.setCursor( new_cursor );
        }

        /*
            Interface to fulfill MouseInputListener()
        */

        public void mouseMoved( MouseEvent mouse_evt ) {}

        public void mouseEntered( MouseEvent mouse_evt )
        {
            super.requestFocus();
            if ( isLeftMouseClick4Zoom )
                super.setCursor( CustomCursor.ZoomPlus );
            else
                super.setCursor( CustomCursor.HandOpen );
        }

        public void mouseExited( MouseEvent mouse_evt )
        {
            /*
               useless to reset cursor here because of similarity of the
               overriden this.setCursor() above and mouseEntered().
            */
            // super.setCursor( CustomCursor.Normal );
        }

        public void mouseClicked( MouseEvent mouse_evt )
        {
            Point  vport_click;
            double focus_time;
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( isLeftMouseClick4Zoom ) {  // Zoom Mode
                    vport_click = mouse_evt.getPoint();
                    focus_time  = coord_xform.convertPixelToTime(
                                              vport_click.x );
                    time_model.setTimeZoomFocus( focus_time );
                    if ( Parameters.LEFTCLICK_INSTANT_ZOOM ) {
                        // Left clicking with Shift to Zoom Out,
                        // Left clinking to Zoom In.
                        if ( mouse_evt.isShiftDown() ) {
                            time_model.zoomOut();
                            super.setCursor( CustomCursor.ZoomMinus );
                        }
                        else {
                            time_model.zoomIn();
                            super.setCursor( CustomCursor.ZoomPlus );
                        }
                        super.requestFocus();
                        if ( toolbar != null )
                            toolbar.resetZoomButtons();
                    }
                    else
                        this.repaint();
                }
            }
            super.requestFocus();
        }

        /* 
            mouse_press_time is a temporary variable among
            mousePressed(), mouseDragged() & mouseReleased()
        */
        private double                    mouse_pressed_time;
        private int                       mouse_pressed_Xloc;
        private int                       mouse_last_Xloc;
        private boolean                   hasControlKeyBeenPressed = false; 

        public void mousePressed( MouseEvent mouse_evt )
        {
            Point  vport_click;
            double click_time;

            // Ignore all mouse events when Control or Escape key is pressed
            if ( mouse_evt.isControlDown() ) {
                hasControlKeyBeenPressed  = true;
                return;
            }

            vport_timebox.setEarliestTime( time_model.getTimeViewPosition() );
            vport_timebox.setLatestFromEarliest(
                          time_model.getTimeViewExtent() );
            coord_xform.resetTimeBounds( vport_timebox );
            vport_click = mouse_evt.getPoint();
            click_time  = coord_xform.convertPixelToTime( vport_click.x );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( isLeftMouseClick4Zoom ) {  // Zoom Mode
                    zoom_timebox = new TimeBoundingBox();
                    zoom_timebox.setZeroDuration( click_time );
                    this.repaint();
                    super.setCursor( CustomCursor.ZoomPlus );
                }
                else  // Hand Mode
                    super.setCursor( CustomCursor.HandClose );
            }
            else if ( SwingUtilities.isRightMouseButton( mouse_evt ) ) {
                info_timebox = new TimeBoundingBox();
                info_timebox.setZeroDuration( click_time );
                this.repaint();
            }
            mouse_pressed_time = click_time;
            mouse_pressed_Xloc = vport_click.x;
            mouse_last_Xloc    = vport_click.x;
        }

        public void mouseDragged( MouseEvent mouse_evt )
        {
            Point  vport_click;
            double click_time;
            double focus_time;

            // Ignore all mouse events when Control key is pressed
            if ( mouse_evt.isControlDown() || hasControlKeyBeenPressed ) {
                hasControlKeyBeenPressed  = true;
                return;
            }

            if ( mouse_evt.isShiftDown() )
                if ( super.getCursor() == CustomCursor.ZoomMinus )
                    super.setCursor( CustomCursor.ZoomPlus );

            vport_click = mouse_evt.getPoint();
            click_time  = coord_xform.convertPixelToTime( vport_click.x );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( isLeftMouseClick4Zoom ) {  // Zoom Mode
                    if ( zoom_timebox != null ) { 
                        // i.e., Zoom has NOT been cancelled yet
                        if ( click_time > mouse_pressed_time )
                            zoom_timebox.setLatestTime( click_time );
                        else
                            zoom_timebox.setEarliestTime( click_time );
                        this.repaint();
                        // super.setCursor( CustomCursor.ZoomPlus );
                    }
                }
                else {  // Hand Mode
                    if ( vport_click.x != mouse_last_Xloc ) {
                        time_model.scroll( mouse_last_Xloc - vport_click.x );
                        mouse_last_Xloc = vport_click.x;
                        super.setCursor( CustomCursor.HandClose );
                    }
                }
            }
            else if ( SwingUtilities.isRightMouseButton( mouse_evt ) ) {
                if ( click_time > mouse_pressed_time )
                    info_timebox.setLatestTime( click_time );
                else
                    info_timebox.setEarliestTime( click_time );
                this.repaint();
            }
        }

        public void mouseReleased( MouseEvent mouse_evt )
        {
            ScrollableObject  scrollable;
            InfoDialog        info_popup;
            Window            window;
            Point             vport_click, view_click, global_click;
            double            click_time, focus_time;

            // Ignore all mouse events when Control key is pressed
            if ( mouse_evt.isControlDown() || hasControlKeyBeenPressed ) {
                hasControlKeyBeenPressed  = false;
                return;
            }

            vport_click = mouse_evt.getPoint();
            click_time  = coord_xform.convertPixelToTime( vport_click.x );
            if ( SwingUtilities.isLeftMouseButton( mouse_evt ) ) {
                if ( isLeftMouseClick4Zoom ) {  // Zoom Mode
                    if ( zoom_timebox != null ) {
                        // i.e., Zoom has NOT been cancelled yet
                        if ( click_time > mouse_pressed_time )
                            zoom_timebox.setLatestTime( click_time );
                        else
                            zoom_timebox.setEarliestTime( click_time );
                        this.repaint();
                        // if ( zoom_timebox.getDuration() > 0.0d ) {
                        if (    Math.abs(vport_click.x - mouse_pressed_Xloc)
                             >= Parameters.MIN_WIDTH_TO_DRAG ) {
                            time_model.zoomRapidly(
                                       zoom_timebox.getEarliestTime(),
                                       zoom_timebox.getDuration() );
                            focus_time = ( zoom_timebox.getEarliestTime()
                                         + zoom_timebox.getLatestTime()
                                         ) / 2.0d;
                            time_model.setTimeZoomFocus( focus_time );
                        }
                        zoom_timebox = null;
                        this.repaint();
                        super.setCursor( CustomCursor.ZoomPlus );
                        if ( toolbar != null )
                            toolbar.resetZoomButtons();
                    }
                }
                else {  // Hand Mode
                    if ( vport_click.x != mouse_last_Xloc ) {
                        time_model.scroll( mouse_last_Xloc - vport_click.x );
                        mouse_last_Xloc = vport_click.x;
                    }
                    super.setCursor( CustomCursor.HandOpen );
                }
            }
            else if ( SwingUtilities.isRightMouseButton( mouse_evt ) ) {
                if ( click_time > mouse_pressed_time )
                    info_timebox.setLatestTime( click_time );
                else
                    info_timebox.setEarliestTime( click_time );
                scrollable = (ScrollableObject) view_img;
                // if ( info_timebox.getDuration() > 0.0d ) {
                if (    Math.abs(vport_click.x - mouse_pressed_Xloc)
                     >= Parameters.MIN_WIDTH_TO_DRAG ) {
                    window = SwingUtilities.windowForComponent( this );
                    if ( window instanceof Frame )
                        info_popup = new InfoDialogForDuration( (Frame) window,
                                                                info_timebox,
                                                                scrollable );
                    else // if ( window instanceof Dialog )
                        info_popup = new InfoDialogForDuration( (Dialog) window,
                                                                info_timebox,
                                                                scrollable );
                }
                else {
                    view_click = SwingUtilities.convertPoint( this,
                                                              vport_click,
                                                              scrollable );
                    info_popup = scrollable.getPropertyAt( view_click,
                                                           vport_timebox );
                }
                global_click = new Point( vport_click );
                SwingUtilities.convertPointToScreen( global_click, this );
                info_popup.setVisibleAtLocation( global_click );
                info_popup.getCloseButton().addActionListener( 
                                            info_action_listener );
                info_popup.addWindowListener( info_window_listener );
                info_dialogs.add( info_popup );
                info_timebox = null;  // remove to avoid redundant drawing
                this.repaint();
            }
        }




        /*
            Interface to fulfill KeyListener()
        */

        public void keyTyped( KeyEvent evt ) {}

        public void keyReleased( KeyEvent evt )
        {
            if ( evt.getKeyCode() == KeyEvent.VK_SHIFT ) {
                if ( super.getCursor() == CustomCursor.ZoomMinus )
                    super.setCursor( CustomCursor.ZoomPlus );
            }
        }

        public void keyPressed( KeyEvent evt )
        {
            if ( evt.getKeyCode() == KeyEvent.VK_SHIFT ) {
                if ( super.getCursor() == CustomCursor.ZoomPlus )
                    super.setCursor( CustomCursor.ZoomMinus );
            }
            else if ( evt.getKeyCode() == KeyEvent.VK_ESCAPE ) {
                if ( zoom_timebox != null ) {
                    zoom_timebox = null;
                    this.repaint();
                }
            }
        }



    public void resetToolBarZoomButtons()
    {
        if ( toolbar != null )
            toolbar.resetZoomButtons();
    }

    protected InfoDialog getLastInfoDialog()
    {
        int info_dialogs_size = info_dialogs.size();
        if ( info_dialogs_size > 0 )
            return (InfoDialog) info_dialogs.get( info_dialogs_size - 1 );
        else
            return null;
    }



    private class InfoDialogActionListener implements ActionListener
    {
        private ViewportTime  viewport;
        private List          info_dialogs;

        public InfoDialogActionListener( ViewportTime vport, List dialogs )
        {
            viewport      = vport;
            info_dialogs  = dialogs;
        }
        
        public void actionPerformed( ActionEvent evt )
        {
            InfoDialog  info_popup;
            Object      evt_src = evt.getSource();
            Iterator itr = info_dialogs.iterator();
            while ( itr.hasNext() ) {
                info_popup = (InfoDialog) itr.next();
                if ( evt_src == info_popup.getCloseButton() ) {
                    info_dialogs.remove( info_popup );
                    info_popup.dispose();
                    viewport.repaint();
                    break;
                }
            }
        }
    }   // InfoDialogActionListener

    private class InfoDialogWindowListener extends WindowAdapter
    {
        private ViewportTime  viewport;
        private List          info_dialogs;

        public InfoDialogWindowListener( ViewportTime vport, List dialogs )
        {
            viewport      = vport;
            info_dialogs  = dialogs;
        }

        public void windowClosing( WindowEvent evt )
        {
            InfoDialog  info_popup;
            Object      evt_src = evt.getSource();
            Iterator itr = info_dialogs.iterator();
            while ( itr.hasNext() ) {
                info_popup = (InfoDialog) itr.next();
                if ( evt_src == info_popup ) {
                    info_dialogs.remove( info_popup );
                    info_popup.dispose();
                    viewport.repaint();
                    break;
                }
            }
        }
    }   // Class InfoDialogWindowListener

}

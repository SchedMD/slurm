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
import javax.swing.border.*;

import viewer.common.Const;
import viewer.common.Parameters;

public class TestMainPanel extends JPanel
{
    private static int              y_row_height
                                    = Parameters.Y_AXIS_ROW_HEIGHT;

    private Component               creator;
    private boolean                 isApplet;

    private TestMainToolBar         toolbar;


    private ModelTime               time_model;
    private ScrollbarTime           time_scrollbar;
    private ModelTimePanel          time_display_panel;

    private RulerTime               time_ruler;
    private ViewportTime            time_ruler_vport;
    private ViewportTimePanel       time_ruler_panel;

    private RulerTime               time_canvas;
    private ViewportTimeYaxis       time_canvas_vport;
    private ViewportTimePanel       time_canvas_panel;



    public TestMainPanel( Component parent )
    {
        super();
        creator      = parent;
        isApplet     = creator.getClass().getSuperclass().getName()
                       .equals( "javax.swing.JApplet" );

        System.out.println( "ScrollBar.MinThumbSize = "
                          + UIManager.get( "ScrollBar.minimumThumbSize" ) );
        System.out.println( "ScrollBar.MaxThumbSize = "
                          + UIManager.get( "ScrollBar.maximumThumbSize" ) );
        Dimension sb_minThumbSz;
        sb_minThumbSz = (Dimension)
                        UIManager.get( "ScrollBar.minimumThumbSize" );
        sb_minThumbSz.width = 1;
        UIManager.put( "ScrollBar.minimumThumbSize", sb_minThumbSz );

        time_model    = new ModelTime( 0.0, 1000.0 ); 
        // time_model    = new ModelTime( 0.89, 1024.5 ); 
        // time_model    = new ModelTime( 0.002091, 0.157824 ); 

        this.setLayout( new BorderLayout() );

            // Setting up the RIGHT panel to store various time-related GUIs
            JPanel right_panel = new JPanel();
            right_panel.setLayout( new BoxLayout( right_panel,
                                                  BoxLayout.Y_AXIS ) );

                // The View's Time Display Panel
                time_display_panel = new ModelTimePanel( time_model );
                time_model.setParamDisplay( time_display_panel );

                // The Time Ruler
                time_ruler        = new RulerTime( time_model );
                time_ruler_vport  = new ViewportTime();
                time_ruler_vport.setView( time_ruler );
                time_ruler_panel  = new ViewportTimePanel( time_ruler_vport );
                time_ruler_panel.setBorderTitle( "Time(seconds)",
                                                 TitledBorder.RIGHT,
                                                 TitledBorder.BOTTOM,
                                                 Const.FONT, Color.red ); 
                /*
                   Propagation of AdjustmentEvent originating from scroller:

                   scroller -----> time_model -----> viewport -----> view
                             adj               time           paint
                   viewport is between time_model and view because
                   viewport is what user sees.  
                */
                time_model.addTimeListener( time_ruler_vport );

                //  Big Time Ruler
                time_canvas       = new RulerTime( time_model );
                time_canvas_vport = new ViewportTimeYaxis();
                time_canvas_vport.setView( time_canvas );
                time_canvas_panel = new ViewportTimePanel( time_canvas_vport );
                time_canvas_panel.setBorderTitle( "BIG RULER",
                                                  TitledBorder.RIGHT,
                                                  TitledBorder.TOP,
                                                  null, null );
                time_model.addTimeListener( time_canvas_vport );

                //  The Horizontal "Time" ScrollBar
                time_scrollbar = new ScrollbarTime( time_model );
                time_scrollbar.setEnabled( true );
                time_model.setScrollBar( time_scrollbar );

            right_panel.add( time_display_panel );
            right_panel.add( time_canvas_panel );
            // right_panel.add( Box.createVerticalGlue() );
            right_panel.add( time_ruler_panel );
            right_panel.add( time_scrollbar );

        this.add( right_panel, BorderLayout.CENTER );

            // The ToolBar for various user controls
            toolbar = new TestMainToolBar( isApplet,
                                           time_scrollbar, time_model );

        this.add( toolbar, BorderLayout.NORTH );

        setVisible( true );

        /*
        // JFrame for vertical ScrollBar extracted from RulerTimeJScrollPane
        // has to be here, cannot be in init(). Don't know why!
        y_scrollbar = time_canvas_panel.getVerticalScrollBar();
        JFrame tmpfm = new JFrame( "Verticl ScrollBar" );
        tmpfm.setContentPane( y_scrollbar );
        tmpfm.setSize( new Dimension( 25, 150 ) );
        tmpfm.addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent e ) {
                System.exit( 0 );
            }
        });
        tmpfm.setVisible( true );
        */
    }

    public void init()
    {
        // time_scrollbar.init();
        if ( Debug.isActive() ) {
            Debug.println( "MainPanel.init(): time_model = "
                         + time_model );
            Debug.println( "MainPanel.init(): time_scrollbar = "
                         + time_scrollbar );
            Debug.println( "MainPanel.init(): time_ruler = "
                         + time_ruler );
        }
        System.out.println( "time_display_panel = " + time_display_panel );
        System.out.println( "time_panel_big.insets = "
                          + time_canvas_panel.getInsets() );
        System.out.println( "time_ruler_panel = " + time_ruler_panel );
        System.out.println( "time_scrollbar = " + time_scrollbar );
    }

}

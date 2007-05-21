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
import java.util.*;
import java.net.URL;

public class TestMainToolBar extends JToolBar
{
    private boolean                 isApplet;
    private ScrollbarTime           time_scrollbar;
    private ModelTime               time_model;

    public  JButton                 mark_btn;
    public  JButton                 move_btn;
    public  JButton                 delete_btn;
    // public  JButton                 redo_btn;
    // public  JButton                 undo_btn;
    // public  JButton                 remove_btn;

    public  JButton                 up_btn;
    public  JButton                 down_btn;
    public  JButton                 expand_btn;
    public  JButton                 collapse_btn;
    public  JButton                 commit_btn;

    public  JButton                 back_btn;
    public  JButton                 forward_btn;
    public  JButton                 zoomIn_btn;
    public  JButton                 home_btn;
    public  JButton                 zoomOut_btn;

    public  JButton                 prefer_btn;
    public  JButton                 print_btn;
    public  JButton                 stop_btn;

    private String                  img_path = "/images/";

    public TestMainToolBar( boolean in_isApplet,
                            ScrollbarTime in_t_scrollbar, ModelTime in_t_model )
    {
        super();
        isApplet         = in_isApplet;
        time_scrollbar   = in_t_scrollbar;
        time_model       = in_t_model;
        this.addButtons();
    }

    public void init()
    {
        this.initButtons();
    }

    protected URL getURL( String filename )
    {
        URL url = null;

        url = getClass().getResource( filename );

        return url;
    }

    private void addButtons()
    {
        Dimension  mini_separator_size;
        URL        icon_URL;

        mini_separator_size = new Dimension( 5, 5 );

        /*
        icon_URL = getURL( img_path + "Up24.gif" );
        if ( icon_URL != null )
            up_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            up_btn = new JButton( "Up" );
        up_btn.setToolTipText( "Up one screen" );
        // up_btn.setPreferredSize( btn_dim );
        up_btn.addActionListener( new ActionVportUp( y_scrollbar ) );
        super.add( up_btn );

        icon_URL = getURL( img_path + "Down24.gif" );
        if ( icon_URL != null )
            down_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            down_btn = new JButton( "Down" );
        down_btn.setToolTipText( "Down one screen" );
        // down_btn.setPreferredSize( btn_dim );
        down_btn.addActionListener( new ActionVportDown( y_scrollbar ) );
        super.add( down_btn );

        super.addSeparator( mini_separator_size );

        icon_URL = getURL( img_path + "Edit24.gif" );
        if ( icon_URL != null )
            mark_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            mark_btn = new JButton( "Mark" );
        mark_btn.setToolTipText( "Mark the timelines" );
        // mark_btn.setPreferredSize( btn_dim );
        mark_btn.addActionListener(
                 new ActionTimelineMark( this, y_tree ) );
        super.add( mark_btn );

        icon_URL = getURL( img_path + "Paste24.gif" );
        if ( icon_URL != null )
            move_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            move_btn = new JButton( "Move" );
        move_btn.setToolTipText( "Move the marked timelines" );
        // move_btn.setPreferredSize( btn_dim );
        move_btn.addActionListener(
                 new ActionTimelineMove( this, y_tree ) );
        super.add( move_btn );

        icon_URL = getURL( img_path + "Delete24.gif" );
        if ( icon_URL != null )
            delete_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            delete_btn = new JButton( "Delete" );
        delete_btn.setToolTipText( "Delete the marked timelines" );
        // delete_btn.setPreferredSize( btn_dim );
        delete_btn.addActionListener(
                   new ActionTimelineDelete( this, y_tree ) );
        super.add( delete_btn );
        */

        /*
        icon_URL = getURL( img_path + "Remove24.gif" );
        if ( icon_URL != null )
            remove_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            remove_btn = new JButton( "Remove" );
        remove_btn.setToolTipText( "Remove the timeline from the display" );
        // remove_btn.setPreferredSize( btn_dim );
        remove_btn.addActionListener(
            new action_timeline_remove( y_tree, list_view ) );
        super.add( remove_btn );
        */

        /*
        super.addSeparator( mini_separator_size );

        icon_URL = getURL( img_path + "TreeExpand24.gif" );
        if ( icon_URL != null )
            expand_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            expand_btn = new JButton( "Expand" );
        expand_btn.setToolTipText( "Expand the tree for 1 level" );
        // expand_btn.setPreferredSize( btn_dim );
        expand_btn.addActionListener(
                   new ActionYaxisTreeExpand( this, y_tree ) );
        super.add( expand_btn );

        icon_URL = getURL( img_path + "TreeCollapse24.gif" );
        if ( icon_URL != null )
            collapse_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            collapse_btn = new JButton( "Collapse" );
        collapse_btn.setToolTipText( "Collapse the tree for 1 level" );
        // collapse_btn.setPreferredSize( btn_dim );
        collapse_btn.addActionListener(
                     new ActionYaxisTreeCollapse( this, y_tree ) );
        super.add( collapse_btn );

        icon_URL = getURL( img_path + "TreeCommit24.gif" );
        if ( icon_URL != null )
            commit_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            commit_btn = new JButton( "Commit" );
        commit_btn.setToolTipText(
                    "Commit the Tree as the Y-axis of the Timeline Display" );
        // collapse_btn.setPreferredSize( btn_dim );
        commit_btn.addActionListener(
                   new ActionYaxisTreeCommit( this, y_maps ) );
        super.add( commit_btn );

        super.addSeparator();
        super.addSeparator();
        */

        icon_URL = getURL( img_path + "Backward24.gif" );
        if ( icon_URL != null )
            back_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            back_btn = new JButton( "Backward" );
        back_btn.setToolTipText( "scroll Backward half a screen" );
        // back_btn.setPreferredSize( btn_dim );
        back_btn.addActionListener( new ActionVportBackward( time_scrollbar ) );
        super.add( back_btn );

        icon_URL = getURL( img_path + "Forward24.gif" );
        if ( icon_URL != null )
            forward_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            forward_btn = new JButton( "Forward" );
        forward_btn.setToolTipText( "scroll Forward half a screen" );
        // forward_btn.setPreferredSize( btn_dim );
        forward_btn.addActionListener(
                    new ActionVportForward( time_scrollbar ) );
        super.add( forward_btn );

        super.addSeparator( mini_separator_size );

        icon_URL = getURL( img_path + "ZoomIn24.gif" );
        if ( icon_URL != null )
            zoomIn_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            zoomIn_btn = new JButton( "ZoomIn" );
        zoomIn_btn.setToolTipText( "Zoom In in Time" );
        // zoomIn_btn.setPreferredSize( btn_dim );
        zoomIn_btn.addActionListener( new ActionZoomIn( null, time_model ) );
        super.add( zoomIn_btn );

        icon_URL = getURL( img_path + "Home24.gif" );
        if ( icon_URL != null )
            home_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            home_btn = new JButton( "Home" );
        home_btn.setToolTipText( "Reset to the lowest resolution in Time" );
        // home_btn.setPreferredSize( btn_dim );
        home_btn.addActionListener( new ActionZoomHome( null, time_model ) );
        super.add( home_btn );

        icon_URL = getURL( img_path + "ZoomOut24.gif" );
        if ( icon_URL != null )
            zoomOut_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            zoomOut_btn = new JButton( "ZoomOut" );
        zoomOut_btn.setToolTipText( "Zoom Out in Time" );
        // zoomOut_btn.setPreferredSize( btn_dim );
        zoomOut_btn.addActionListener( new ActionZoomOut( null, time_model ) );
        super.add( zoomOut_btn );

        super.addSeparator();
        super.addSeparator();

        icon_URL = getURL( img_path + "Preferences24.gif" );
        if ( icon_URL != null )
            prefer_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            prefer_btn = new JButton( "Preferences" );
        prefer_btn.setToolTipText( "Preferences" );
        // prefer_btn.setPreferredSize( btn_dim );
        // prefer_btn.addActionListener( new ActionPptyPrefer() );
        super.add( prefer_btn );

        icon_URL = getURL( img_path + "Print24.gif" );
        if ( icon_URL != null )
            print_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            print_btn = new JButton( "Print" );
        print_btn.setToolTipText( "Print" );
        // print_btn.setPreferredSize( btn_dim );
        print_btn.addActionListener( new ActionPptyPrint() );
        super.add( print_btn );

        icon_URL = getURL( img_path + "Stop24.gif" );
        if ( icon_URL != null )
            stop_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            stop_btn = new JButton( "Exit" );
        stop_btn.setToolTipText( "Exit" );
        // stop_btn.setPreferredSize( btn_dim );
        stop_btn.addActionListener( new ActionPptyStop() );
        super.add( stop_btn );
    }

    private void initButtons()
    {
        /*
        up_btn.setEnabled( true );
        down_btn.setEnabled( true );

        mark_btn.setEnabled( true );
        move_btn.setEnabled( false );
        delete_btn.setEnabled( false );
        // remove_btn.setEnabled( true );

        expand_btn.setEnabled( y_tree.isLevelExpandable() );
        collapse_btn.setEnabled( y_tree.isLevelCollapsable() );
        commit_btn.setEnabled( true );
        */

        // back_btn.setEnabled( false );
        // forward_btn.setEnabled( false );
        // zoomIn_btn.setEnabled( true );
        // home_btn.setEnabled( false );
        // zoomOut_btn.setEnabled( false );

        prefer_btn.setEnabled( true );
        print_btn.setEnabled( true );
        if ( isApplet )
            stop_btn.setEnabled( false );
    }
}

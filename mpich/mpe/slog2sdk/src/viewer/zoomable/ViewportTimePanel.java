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
import javax.swing.border.*;

import viewer.common.Routines;

public class ViewportTimePanel extends JPanel
{
    private ViewportTime   viewport;

    private static Border  border;
    private String         borderTitle;
    private int            borderTitleJustification;
    private int            borderTitlePosition;
    private Font           borderTitleFont;
    private Color          borderTitleColor;

    public ViewportTimePanel( final ViewportTime  vport )
    {
        super( new BorderLayout() );
        viewport  = vport;
        // viewport.setScrollMode( JViewport.BLIT_SCROLL_MODE );
        // viewport.setScrollMode( JViewport.BACKINGSTORE_SCROLL_MODE );
        // viewport.setScrollMode( JViewport.SIMPLE_SCROLL_MODE );
        super.add( viewport, BorderLayout.CENTER );
        super.setBackground( Color.white );

        // Default border
        if ( border == null ) {
            border = BorderFactory.createCompoundBorder(
                                   BorderFactory.createRaisedBevelBorder(),
                                   BorderFactory.createLoweredBevelBorder() );
            // border = BorderFactory.createEmptyBorder();
        }
        borderTitle              = null;
        borderTitleJustification = TitledBorder.DEFAULT_JUSTIFICATION;
        borderTitlePosition      = TitledBorder.DEFAULT_POSITION;
        borderTitleFont          = null;
        borderTitleColor         = null;
    }

    /*
    public ViewportTime getTimeViewport()
    {
        return viewport;
    }
    */

    public static void setDefaultBorder( final Border bdr )
    {
        border = bdr;
    }

    public void setBorderTitle( String title,
                                int    titleJustification,
                                int    titlePosition,
                                Font   titleFont,
                                Color  titleColor )
    {
        if ( title != null ) {
            borderTitle               = title; 
            borderTitleJustification  = titleJustification;
            borderTitlePosition       = titlePosition;
            borderTitleFont           = titleFont;
            borderTitleColor          = titleColor;
        }
        this.setBorder();
    }

    private void setBorder()
    {
        Border tbdr;
        if ( borderTitle != null ) {
            tbdr = BorderFactory.createTitledBorder( border, borderTitle,
                                 borderTitleJustification, borderTitlePosition,
                                 borderTitleFont, borderTitleColor );
        }
        else
            tbdr = border;
        super.setBorder( tbdr );

        if ( viewport != null ) {
            Dimension min_size, max_size;
            Insets    insets = this.getInsets();
            if ( Debug.isActive() )
                Debug.println( "ViewportTimePanel(): this.insets = " + insets );
            min_size  = Routines.correctSize( viewport.getMinimumSize(),
                                              insets );
            super.setMinimumSize( min_size );
            max_size  = Routines.correctSize( viewport.getMaximumSize(),
                                              insets );
            super.setMaximumSize( max_size );
        }
    }

    // Fixing the minimum size of this panel
    public Dimension getMinimumSize()
    {
        Dimension min_size = super.getMinimumSize();
        if ( Debug.isActive() )
            Debug.println( "ViewportTimePanel(): min_size = " + min_size );
        return min_size;
    }

    // This is just for the time_ruler
    // Fixing the maximum size of this panel
    public Dimension getMaximumSize()
    {
        Dimension max_size = super.getMaximumSize();
        if ( Debug.isActive() )
            Debug.println( "ViewportTimePanel(): max_size = " + max_size );
        return max_size;
    }

    // Fixing the preferred size of this panel
    public Dimension getPreferredSize()
    {
        Dimension pref_size = super.getPreferredSize();
        if ( Debug.isActive() )
            Debug.println( "ViewportTimePanel(): pref_size = " + pref_size );
        return pref_size;
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.awt.Color;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.Component;
import javax.swing.Icon;

import base.drawable.Category;
import base.drawable.Topology;
import base.drawable.ColorAlpha;
import base.topology.StateBorder;
import viewer.common.Dialogs;
import viewer.common.Parameters;

public class CategoryIcon implements Icon
{
    public  static final CategoryIcon  BLANK_ICON    = new CategoryIcon();

    private static final int    ICON_WIDTH           = Const.ICON_WIDTH;
    private static final int    ICON_HEIGHT          = Const.ICON_HEIGHT;
    private static final int    ICON_HALF_WIDTH      = Const.ICON_WIDTH / 2;
    private static final int    ICON_QUARTER_WIDTH   = Const.ICON_WIDTH / 4;
    private static final int    ICON_HALF_HEIGHT     = Const.ICON_HEIGHT / 2;
    private static final int    ICON_QUARTER_HEIGHT  = Const.ICON_HEIGHT / 4;
    private static final int    XOFF                 = 3;
    private static final int    YOFF                 = 2;

    private       Topology    topo;
    private final ColorAlpha  color_orig;
    private       Color       color_shown;

    private CategoryIcon()
    {
        topo        = null;
        color_orig  = null;
        color_shown = null;
    }

    public CategoryIcon( Category type )
    {
        topo        = type.getTopology();
        color_orig  = type.getColor();
        color_shown = (Color) color_orig;
    }

    public CategoryIcon( Topology shape, Color color )
    {
        topo        = shape;
        color_orig  = new ColorAlpha( color, ColorAlpha.OPAQUE );
        color_shown = color;
    }

    public void setDisplayedColor( Color new_color )
    {
        color_shown = new_color;
    }

    public Color getDisplayedColor()
    {
        return color_shown;
    }

    public ColorAlpha getOriginalColor()
    {
        return color_orig;
    }

    public void resetColor()
    {
        color_shown = (Color) color_orig;
    }

    public int getIconWidth()
    {
        return ICON_WIDTH;
    }

    public int getIconHeight()
    {
        return ICON_HEIGHT;
    }

    public void paintIcon( Component cmpo, Graphics g, int x, int y )
    {
        Color old_color = g.getColor();
        if ( topo != null ) {
            if ( topo.isEvent() )
                this.paintEventIcon( g, x, y );
            else if ( topo.isState() )
                this.paintStateIcon( g, x, y );
            else if ( topo.isArrow() )
                this.paintArrowIcon( g, x, y );
            else
                this.paintBlankIcon( g, x, y );
        }
        else
            this.paintBlankIcon( g, x, y );
        g.setColor( old_color );
    }

    private void paintStateIcon( Graphics g, int x, int y )
    {
        int x1, y1, x2, y2;

        // Paint the background
        g.setColor( (Color) Parameters.BACKGROUND_COLOR.toValue() );
        g.fillRect( x, y, ICON_WIDTH, ICON_HEIGHT );

        // Paint middle timeline
        x1 = x ;                       y1 = y + ICON_HALF_HEIGHT;
        x2 = x + ICON_WIDTH - 1;       y2 = y1;
        g.setColor( Color.red );
        g.drawLine( x1, y1, x2, y2 );

        x1 = x + XOFF;                   y1 = y + YOFF;
        x2 = x1 + ICON_WIDTH-1-2*XOFF;   y2 = y1 + ICON_HEIGHT-1-2*YOFF;

        // Paint the state's color
        g.setColor( color_shown );
        g.fillRect( x1, y1, ICON_WIDTH-2*XOFF, ICON_HEIGHT-2*YOFF );

        // Paint the border
        /*
        g.setColor( Color.white );
        g.drawLine( x1, y1, x1, y2 );     // left
        g.drawLine( x1, y1, x2, y1 );     // top
        g.setColor( Color.lightGray );
        g.drawLine( x2, y1, x2, y2 );     // right
        g.drawLine( x1, y2, x2, y2 );     // bottom
        */
        Parameters.STATE_BORDER.paintStateBorder( (Graphics2D) g,
                                                  color_shown,
                                                  x1, y1, true,
                                                  x2, y2, true );
    }

    private void paintArrowIcon( Graphics g, int x, int y )
    {
        int x1, y1, x2, y2, x3, y3;

        // g.setColor( Color.black );
        g.setColor( (Color) Parameters.BACKGROUND_COLOR.toValue() );
        g.fillRect( x, y, ICON_WIDTH, ICON_HEIGHT );

        g.setColor( color_shown );

        /* Draw the arrow body */
        x1 = x ;                       y1 = y + ICON_HALF_HEIGHT;
        x2 = x + ICON_WIDTH - 1;       y2 = y1;
        g.drawLine( x1, y1, x2, y2 );
        
        /* Draw the arrow head */
        x1 = x2;                       y1 = y2;
        x2 = x1 - ICON_QUARTER_WIDTH ; y2 = y1 + ICON_QUARTER_HEIGHT; 
        x3 = x2;                       y3 = y1 - ICON_QUARTER_HEIGHT; 
        g.drawLine( x1, y1, x2, y2 );
        g.drawLine( x1, y1, x3, y3 );
        g.drawLine( x2, y2, x3, y3 );
    }

    private void paintEventIcon( Graphics g, int x, int y )
    {
        int x1, y1, x2, y2, x3, y3;

        // g.setColor( Color.black );
        g.setColor( (Color) Parameters.BACKGROUND_COLOR.toValue() );
        g.fillRect( x, y, ICON_WIDTH, ICON_HEIGHT );

        // Paint middle timeline
        x1 = x ;                       y1 = y + ICON_HALF_HEIGHT;
        x2 = x + ICON_WIDTH - 1;       y2 = y1;
        g.setColor( Color.red );
        g.drawLine( x1, y1, x2, y2 );

        x1 = x + XOFF;                   y1 = y + YOFF;

        // Fill the ellipse
        g.setColor( color_shown );
        g.fillArc( x1, y1, ICON_WIDTH-2*XOFF, ICON_QUARTER_HEIGHT, 0, 360 );

        // Draw the ellipse
        g.setColor( Color.white );
        g.drawArc( x1, y1, ICON_WIDTH-2*XOFF, ICON_QUARTER_HEIGHT, 0, 360 );

        // Draw the line marks the event
        x1 = x + ICON_HALF_WIDTH;        y1 = y + YOFF + ICON_QUARTER_HEIGHT;
        x2 = x1;                         y2 = y + ICON_HEIGHT-1 - 2;
        g.drawLine( x1, y1, x2, y2 );
    }
/*
    private void paintEventIcon( Graphics g, int x, int y )
    {
        int x1, y1, x2, y2, x3, y3;

        // g.setColor( Color.black );
        g.setColor( (Color) Parameters.BACKGROUND_COLOR.toValue() );
        g.fillRect( x, y, ICON_WIDTH, ICON_HEIGHT );

        g.setColor( color_shown );

        int half_base_width = ICON_QUARTER_WIDTH - 2;
        // Draw the sym triangle
        x1 = x + ICON_HALF_WIDTH;                y1 = y + 2;
        x2 = x + half_base_width;                y2 = y + ICON_HEIGHT-1 - 2;
        x3 = x + ICON_WIDTH-1 - half_base_width; y3 = y2;
        g.drawLine( x1, y1, x2, y2 );
        g.drawLine( x1, y1, x3, y3 );
        g.drawLine( x2, y2, x3, y3 );
    }
*/

    private void paintBlankIcon( Graphics g, int x, int y )
    {
        // g.setColor( Color.black );
        g.setColor( (Color) Parameters.BACKGROUND_COLOR.toValue() );
        g.fillRect( x, y, ICON_WIDTH, ICON_HEIGHT );
    }

}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.awt.Color;
import java.awt.Dimension;
import javax.swing.JLabel;
import javax.swing.SwingConstants;

import base.drawable.Category;
import base.drawable.Topology;

public class CategoryLabel extends JLabel
{
    public CategoryLabel( final Category objdef )
    {
        super( objdef.getName(), new CategoryIcon( objdef ),
               SwingConstants.LEFT );
        super.setIconTextGap( 2 * Const.CELL_ICON_TEXT_GAP );
    }

    public CategoryLabel( String name, Topology topo, Color color )
    {
        super( name, new CategoryIcon( topo, color ),
               SwingConstants.LEFT );
        super.setIconTextGap( 2 * Const.CELL_ICON_TEXT_GAP );
    }

    public Dimension getPreferredSize()
    {
        Dimension pref_sz = super.getPreferredSize();
        return new Dimension( pref_sz.width, Const.CELL_HEIGHT );
    }
}

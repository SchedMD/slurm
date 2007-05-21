/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.awt.Color;
import java.awt.Component;
import javax.swing.SwingConstants;
import javax.swing.border.Border;
import javax.swing.BorderFactory;
import javax.swing.Icon;
import javax.swing.JLabel;
import javax.swing.JTable;
import javax.swing.table.TableCellRenderer;

public class CategoryIconRenderer extends JLabel
                                  implements TableCellRenderer
{
    private static final Color  CELL_BACKCOLOR
                                = Const.CELL_BACKCOLOR;
    private static final Color  CELL_FORECOLOR
                                = Const.CELL_FORECOLOR;
    private static final Color  CELL_BACKCOLOR_SELECTED
                                = Const.CELL_BACKCOLOR_SELECTED;
    private static final Color  CELL_FORECOLOR_SELECTED
                                = Const.CELL_FORECOLOR_SELECTED;

    private Border  raised_border, lowered_border;

    public CategoryIconRenderer()
    {
        super();
        super.setOpaque( true );
        super.setHorizontalAlignment( SwingConstants.CENTER );
        super.setIconTextGap( Const.CELL_ICON_TEXT_GAP );
        raised_border  = BorderFactory.createRaisedBevelBorder();
        lowered_border = BorderFactory.createLoweredBevelBorder();
    }

    public Component getTableCellRendererComponent( JTable   table,
                                                    Object   value,
                                                    boolean  isSelected,
                                                    boolean  hasFocus,
                                                    int      irow,
                                                    int      icolumn )
    {
        super.setIcon( (Icon) value );
        if ( isSelected ) {
            super.setForeground( CELL_FORECOLOR_SELECTED );
            super.setBackground( CELL_BACKCOLOR_SELECTED );
            super.setBorder( lowered_border );
        }
        else {
            super.setForeground( CELL_FORECOLOR );
            super.setBackground( CELL_BACKCOLOR );
            super.setBorder( raised_border );
        }
        // repaint();
        return this;
    }
}

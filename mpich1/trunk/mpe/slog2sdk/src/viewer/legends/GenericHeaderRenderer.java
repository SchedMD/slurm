/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.awt.Point;
import java.awt.Color;
import java.awt.event.MouseEvent;
import java.awt.event.MouseAdapter;
import javax.swing.Icon;
import javax.swing.border.Border;
import javax.swing.BorderFactory;
import javax.swing.SwingConstants;
import javax.swing.JTable;
import javax.swing.table.TableModel;
import javax.swing.table.JTableHeader;
import javax.swing.table.DefaultTableCellRenderer;

public class GenericHeaderRenderer extends DefaultTableCellRenderer
{
    private static final Border BORDER
                                = BorderFactory.createEmptyBorder( 1,3,1,3 );

    private JTable             table_view;
    private int                renderer_column;

    private LegendTableModel   table_model;
    private JTableHeader       table_header;
    private Color              released_bg_color;
    private Color              pressed_bg_color;
    private Icon               released_tab_icon;
    private Icon               pressed_tab_icon;

 
    public GenericHeaderRenderer( LegendTable in_table, int icolumn )
    {
        super();
        table_view       = in_table;
        renderer_column  = icolumn;

        table_model = (LegendTableModel) table_view.getModel();
        super.setText( table_model.getColumnName( icolumn ) );
        super.setToolTipText( table_model.getColumnToolTip( icolumn ) );
        super.setHorizontalAlignment( SwingConstants.CENTER );

        super.setForeground( table_model.getColumnNameForeground( icolumn ) );
        released_bg_color  = table_model.getColumnNameBackground( icolumn );
        pressed_bg_color   = released_bg_color.darker();
        super.setBackground( released_bg_color );

        table_header       = null;
        released_tab_icon  = null;
        pressed_tab_icon   = null;
    }

    /*
        If this header renderer needs to have a pulldown tab,
        this.initPressablePullDownTab() has to be called to set up the
        simulated pulldown tab.
    */
    public void initPressablePullDownTab()
    {
        boolean  is_raised_tab;
        is_raised_tab  = table_model.isRaisedColumnNameIcon( renderer_column );
        released_tab_icon  = new Triangular3DIcon( Triangular3DIcon.DOWN,
                                                   true, is_raised_tab );
        pressed_tab_icon   = new Triangular3DIcon( Triangular3DIcon.DOWN,
                                                   false, is_raised_tab );
        table_header  = table_view.getTableHeader();
        super.setHorizontalTextPosition( SwingConstants.LEFT );
        super.setIcon( released_tab_icon );
        //  Renderer is a RubberStamp class, it does not get any MouseEvent
        table_header.addMouseListener( new RendererMouseHandler() );
        // super.setBorder( BORDER );
    }

    public void setPressed( boolean isPressed )
    {
        if ( isPressed ) {
            super.setIcon( pressed_tab_icon );
            super.setBackground( pressed_bg_color );
        }
        else {
            super.setIcon( released_tab_icon );
            super.setBackground( released_bg_color );
        }
        super.revalidate();
        super.repaint();
        table_header.repaint(); 
    }


    private class RendererMouseHandler extends MouseAdapter
    {
        private GenericHeaderRenderer  renderer;

        public RendererMouseHandler()
        {
            renderer  = GenericHeaderRenderer.this;
        }

        private boolean isMouseEventAtMyColumn( MouseEvent evt )
        {
            Point      click;
            int        click_column, model_column;

            click        = evt.getPoint();
            click_column = table_header.columnAtPoint( click );
            model_column = table_view.convertColumnIndexToModel( click_column );
            return model_column == renderer_column;
        }

        public void mousePressed( MouseEvent evt )
        {
            if ( isMouseEventAtMyColumn( evt ) )
                renderer.setPressed( true );
        }

        public void mouseReleased( MouseEvent evt )
        {
            if ( isMouseEventAtMyColumn( evt ) )
                renderer.setPressed( false );
        }
    }
}

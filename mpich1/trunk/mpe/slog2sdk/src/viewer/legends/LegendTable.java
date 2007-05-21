/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.util.Map;
import java.awt.Insets;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Component;
import java.awt.event.MouseAdapter;
import javax.swing.SwingConstants;
import javax.swing.JComponent;
import javax.swing.Icon;
import javax.swing.JLabel;
import javax.swing.JCheckBox;
import javax.swing.JPopupMenu;
import javax.swing.JTable;
import javax.swing.table.TableColumn;
import javax.swing.table.TableColumnModel;
import javax.swing.table.JTableHeader;
import javax.swing.table.TableCellRenderer;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.UIManager;

import base.drawable.Category;
import logformat.slog2.CategoryMap;

public class LegendTable extends JTable
{
    private static final Insets EMPTY_INSETS
                                = new Insets( 0, 0, 0, 0 );
    private static final Color  CELL_BACKCOLOR
                                = Const.CELL_BACKCOLOR;
    private static final Color  CELL_FORECOLOR
                                = Const.CELL_FORECOLOR;
    private static final Color  CELL_BACKCOLOR_SELECTED
                                = Const.CELL_BACKCOLOR_SELECTED;
    private static final Color  CELL_FORECOLOR_SELECTED
                                = Const.CELL_FORECOLOR_SELECTED;

    private LegendTableModel    table_model;
    private TableColumnModel    column_model;
    private JTableHeader        table_header;

    public LegendTable( CategoryMap  map )
    {
        super();

        table_model = new LegendTableModel( map );
        super.setModel( table_model );
        super.setDefaultRenderer( CategoryIcon.class,
                                  new CategoryIconRenderer() );
        super.setDefaultEditor( CategoryIcon.class,
                                new CategoryIconEditor() );
        super.setAutoResizeMode( AUTO_RESIZE_OFF );
        super.setIntercellSpacing( new Dimension( 2, 2 ) );
        super.setShowHorizontalLines( false );
        super.setShowVerticalLines( true );

        column_model  = super.getColumnModel();
        table_header = this.getTableHeader();
        this.setColumnHeaderRenderers();
        this.initColumnSize();

        // super.getSelectionModel().addListSelectionListener( table_model );
    }

    private void setColumnHeaderRenderers()
    {
        TableColumn        column; 
        TableCellRenderer  renderer;
        JPopupMenu         pop_menu;
        MouseAdapter       handler;
        Color              bg_color;
        Class              class_type;
        int                column_count;

        column_count  = table_model.getColumnCount();
        for ( int icol = 0; icol < column_count; icol++ ) {
            column     = column_model.getColumn( icol );
            renderer   = column.getHeaderRenderer();
            class_type = table_model.getColumnClass( icol );
            if ( class_type == Boolean.class ) {
                renderer = new GenericHeaderRenderer( this, icol );
                ((GenericHeaderRenderer) renderer).initPressablePullDownTab();
                column.setHeaderRenderer( renderer );

                pop_menu = new OperationBooleanMenu( this, icol );
                handler  = new TableHeaderHandler( this, icol, pop_menu );
                table_header.addMouseListener( handler );
                handler  = new TableColumnHandler( this, icol, pop_menu );
                this.addMouseListener( handler );
            }
            if ( class_type == String.class ) {
                renderer = new GenericHeaderRenderer( this, icol );
                ((GenericHeaderRenderer) renderer).initPressablePullDownTab();
                column.setHeaderRenderer( renderer );

                pop_menu = new OperationStringMenu( this, icol );
                handler  = new TableHeaderHandler( this, icol, pop_menu );
                table_header.addMouseListener( handler );
                handler  = new TableColumnHandler( this, icol, pop_menu );
                this.addMouseListener( handler );
            }
            else if ( renderer == null ) {
                renderer = new GenericHeaderRenderer( this, icol );
                column.setHeaderRenderer( renderer );
            }
            else
                ( (JComponent) renderer).setToolTipText(
                               table_model.getColumnToolTip( icol ) );
        }
    }

    private void initColumnSize()
    {
        TableCellRenderer  renderer;
        Component          component;
        TableColumn        column; 
        Dimension          intercell_gap;
        Object             header_value;
        Dimension          header_size;
        Insets             header_insets;
        int                header_width;
        Dimension          cell_size;
        Insets             cell_insets;
        int                cell_width, cell_height, row_height;
        int                column_count, row_count;
        int                vport_width, vport_height;

        vport_width    = 0;
        vport_height   = 0;

        row_height     = 0;
        intercell_gap  = super.getIntercellSpacing();
        column_count   = table_model.getColumnCount();
        for ( int icol = 0; icol < column_count; icol++ ) {
            column        = column_model.getColumn( icol );
            // determine header renderer's size
            renderer      = column.getHeaderRenderer();
            component     = renderer.getTableCellRendererComponent( this,
                                     column.getHeaderValue(),
                                     false, false, -1, icol );
            header_size   = component.getPreferredSize();
            header_insets = ( (JComponent) component ).getInsets();
            header_width  = header_size.width + intercell_gap.width
                          + header_insets.left + header_insets.right;
            // determine cell renderer's size
            renderer     = column.getCellRenderer();
            if ( renderer == null )
                renderer = super.getDefaultRenderer(
                                 table_model.getColumnClass( icol ) );
            component   = renderer.getTableCellRendererComponent( this,
                                   table_model.getColumnTypicalValue( icol ),
                                   false, false, 0, icol );
            cell_size   = component.getPreferredSize();
            // cell_insets = ( (JComponent) component ).getInsets();
            if ( component instanceof CategoryIconRenderer )
                cell_insets = ( (JComponent) component ).getInsets();
            else
                cell_insets = EMPTY_INSETS;
            cell_width   = cell_size.width
                         + cell_insets.left + cell_insets.right;
            /*
            System.out.println( "At column " + icol + "\n"
                              + "\t header size = " + header_size + "\n"
                              + "\t cell size = " + cell_size );
            System.out.println( "\t header_width = " + header_width
                              + ", cell_width = " + cell_width );
            */
            if ( cell_width > header_width ) {
                column.setPreferredWidth( cell_width );
                vport_width  += cell_width;
            }
            else {
                column.setPreferredWidth( header_width );
                vport_width  += header_width;
            }
            cell_height   = cell_size.height
                          + cell_insets.top + cell_insets.bottom;
            if ( cell_height > row_height )
                row_height  = cell_height;
        }
        super.setRowHeight( row_height );

        row_count     = table_model.getRowCount();
        if ( row_count > Const.LIST_MAX_VISIBLE_ROW_COUNT )
            vport_height  = row_height * Const.LIST_MAX_VISIBLE_ROW_COUNT;
        else
            vport_height  = row_height * row_count;
        super.setPreferredScrollableViewportSize(
              new Dimension( vport_width, vport_height ) );
    }
}

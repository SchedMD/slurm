/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.net.URL;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.Icon;
import javax.swing.ImageIcon;
import javax.swing.JPopupMenu;
import javax.swing.JMenuItem;
import javax.swing.JTable;
import javax.swing.table.TableModel;

import viewer.common.Const;
/*
   Class to simulate a JMenuBar header editor for a JTable with boolean value
*/
public class OperationBooleanMenu extends JPopupMenu
{
    private static String     toggle_selected_icon_path
                              = Const.IMG_PATH + "checkbox/ToggleSelected.gif";
    private static String     enable_selected_icon_path
                              = Const.IMG_PATH + "checkbox/EnableSelected.gif";
    private static String     disable_selected_icon_path
                              = Const.IMG_PATH + "checkbox/DisableSelected.gif";
    private static String     toggle_all_icon_path
                              = Const.IMG_PATH + "checkbox/ToggleAll.gif";
    private static String     enable_all_icon_path
                              = Const.IMG_PATH + "checkbox/EnableAll.gif";
    private static String     disable_all_icon_path
                              = Const.IMG_PATH + "checkbox/DisableAll.gif";

    private JTable            table_view;
    private LegendTableModel  table_model;
    private int               bool_column;  // index where Boolean.class is

    public OperationBooleanMenu( JTable in_table, int in_column )
    {
        super();
        table_view  = in_table;
        table_model = (LegendTableModel) table_view.getModel();
        bool_column = in_column;

        super.setLabel( table_model.getColumnName( bool_column ) );
        super.setToolTipText( table_model.getColumnToolTip( bool_column ) );
        this.addMenuItems();
    }

    private void addMenuItems()
    {
        JMenuItem  menu_item;
        URL        icon_URL;
        Icon       icon;

            icon_URL = null;
            icon_URL = getURL( toggle_selected_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item  = new JMenuItem( "Toggle Selected", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { toggleSelectedAtColumn( bool_column ); }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( enable_selected_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item  = new JMenuItem( "Enable Selected", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { setSelectedAtColumn( bool_column, Boolean.TRUE ); }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( disable_selected_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item  = new JMenuItem( "Disable Selected", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { setSelectedAtColumn( bool_column, Boolean.FALSE ); }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( toggle_all_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item  = new JMenuItem( "Toggle All", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { toggleAllAtColumn( bool_column ); }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( enable_all_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item        = new JMenuItem( "Enable All", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { setAllAtColumn( bool_column, Boolean.TRUE ); }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( disable_all_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item       = new JMenuItem( "Disable All", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { setAllAtColumn( bool_column, Boolean.FALSE ); }
            } );
        super.add( menu_item );
    }


    /*
       4 private methods for addMenuItem()
    */
    private void toggleSelectedAtColumn( int icolumn )
    {
        int[]      irows;
        int        irows_length, irow, idx;
        Boolean    bval;

        irows         = table_view.getSelectedRows();
        irows_length  = irows.length;
        for ( idx = 0; idx < irows_length; idx++ ) {
             irow  = irows[ idx ];
             bval  = (Boolean) table_model.getValueAt( irow, icolumn );
             if ( bval.booleanValue() )
                 table_model.setValueAt( Boolean.FALSE, irow, icolumn );
             else
                 table_model.setValueAt( Boolean.TRUE, irow, icolumn );
        }
    }

    private void setSelectedAtColumn( int icolumn, Boolean bval )
    {
        int[]      irows;
        int        irows_length, irow, idx;

        irows         = table_view.getSelectedRows();
        irows_length  = irows.length;
        for ( idx = 0; idx < irows_length; idx++ ) {
             irow  = irows[ idx ];
             table_model.setValueAt( bval, irow, icolumn );
        }
    }

    private void toggleAllAtColumn( int icolumn )
    {
        int        irows_length, irow;
        Boolean    bval;

        irows_length  = table_model.getRowCount();
        for ( irow = 0; irow < irows_length; irow++ ) {
             bval  = (Boolean) table_model.getValueAt( irow, icolumn );
             if ( bval.booleanValue() )
                 table_model.setValueAt( Boolean.FALSE, irow, icolumn );
             else
                 table_model.setValueAt( Boolean.TRUE, irow, icolumn );
        }
    }

    private void setAllAtColumn( int icolumn, Boolean bval )
    {
        int        irows_length, irow;

        irows_length  = table_model.getRowCount();
        for ( irow = 0; irow < irows_length; irow++ ) {
             table_model.setValueAt( bval, irow, icolumn );
        }
    }



    private URL getURL( String filename )
    {
        return getClass().getResource( filename );
    }
}

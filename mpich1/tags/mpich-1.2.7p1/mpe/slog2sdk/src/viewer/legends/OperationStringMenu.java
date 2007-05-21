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
   Class to simulate a JMenuBar header editor for a JTable with String value
*/
public class OperationStringMenu extends JPopupMenu
{
    private static String     index_order_icon_path
                              = Const.IMG_PATH
                              + "checkbox/IndexOrder.gif";
    private static String     case_sensitive_topo_order_icon_path
                              = Const.IMG_PATH
                              + "checkbox/CaseSensitiveTopoOrder.gif";
    private static String     case_insensitive_topo_order_icon_path
                              = Const.IMG_PATH
                              + "checkbox/CaseInsensitiveTopoOrder.gif";
    private static String     case_sensitive_order_icon_path
                              = Const.IMG_PATH
                              + "checkbox/CaseSensitiveOrder.gif";
    private static String     case_insensitive_order_icon_path
                              = Const.IMG_PATH
                              + "checkbox/CaseInsensitiveOrder.gif";
    private static String     reverse_order_icon_path
                              = Const.IMG_PATH
                              + "checkbox/ReverseOrder.gif";

    private JTable            table_view;
    private LegendTableModel  table_model;
    private int               str_column;  // index where String.class is

    public OperationStringMenu( JTable in_table, int in_column )
    {
        super();
        table_view  = in_table;
        table_model = (LegendTableModel) table_view.getModel();
        str_column  = in_column;

        super.setLabel( table_model.getColumnName( str_column ) );
        super.setToolTipText( table_model.getColumnToolTip( str_column ) );
        this.addMenuItems();
    }

    private void addMenuItems()
    {
        JMenuItem  menu_item;
        URL        icon_URL;
        Icon       icon;

            icon_URL = null;
            icon_URL = getURL( case_sensitive_topo_order_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item = new JMenuItem( "A...Z a...z", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { table_model.arrangeOrder(
                              LegendComparators.CASE_SENSITIVE_ORDER ); }
            } );
        super.add( menu_item );

           icon_URL = null;
            icon_URL = getURL( case_sensitive_topo_order_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item = new JMenuItem( "z...a Z...A", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { table_model.reverseOrder(
                              LegendComparators.CASE_SENSITIVE_ORDER );
                }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( case_sensitive_topo_order_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item = new JMenuItem( "Aa...Zz", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { table_model.arrangeOrder(
                              LegendComparators.CASE_INSENSITIVE_ORDER ); }
            } );
        super.add( menu_item );

           icon_URL = null;
            icon_URL = getURL( case_sensitive_topo_order_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item = new JMenuItem( "zZ...aA", icon );
            menu_item.addActionListener( new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { table_model.reverseOrder(
                              LegendComparators.CASE_INSENSITIVE_ORDER );
                }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( index_order_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item   = new JMenuItem( "Creation Order", icon );
            menu_item.addActionListener(
            new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { table_model.arrangeOrder( LegendComparators.INDEX_ORDER ); }
            } );
        super.add( menu_item );

            icon_URL = null;
            icon_URL = getURL( index_order_icon_path );
            if ( icon_URL != null )
                icon = new ImageIcon( icon_URL );
            else
                icon = null;
            menu_item   = new JMenuItem( "Reverse Creation Order", icon );
            menu_item.addActionListener(
            new ActionListener() {
                public void actionPerformed( ActionEvent evt )
                { table_model.reverseOrder( LegendComparators.INDEX_ORDER ); }
            } );
        super.add( menu_item );
    }

    private URL getURL( String filename )
    {
        return getClass().getResource( filename );
    }
}

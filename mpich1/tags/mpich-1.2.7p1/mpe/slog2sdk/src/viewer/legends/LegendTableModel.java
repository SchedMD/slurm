/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.util.Map;
import java.util.Iterator;
import java.util.List;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Collections;

import java.awt.Color;
import javax.swing.Icon;
import javax.swing.ImageIcon;
import javax.swing.table.AbstractTableModel;
import javax.swing.ListSelectionModel;
// import javax.swing.event.ListSelectionListener;
// import javax.swing.event.ListSelectionEvent;

import base.drawable.ColorAlpha;
import base.drawable.Category;
import logformat.slog2.CategoryMap;

public class LegendTableModel extends AbstractTableModel
//                              implements ListSelectionListener
{
    public  static final int        ICON_COLUMN            = 0;
    public  static final int        NAME_COLUMN            = 1;
    public  static final int        VISIBILITY_COLUMN      = 2;
    public  static final int        SEARCHABILITY_COLUMN   = 3;

    private static final String[]   COLUMN_TITLES
                                    = { "Topo", "Name", "V", "S" };
    private static final String[]   COLUMN_TOOLTIPS
                                    = { "Topology/Color", "Category Name",
                                        "Visibility", "Searchability" };
    private static final Class[]    COLUMN_CLASSES
                                    = { CategoryIcon.class, String.class,
                                        Boolean.class, Boolean.class };
    private static final Color[]    COLUMN_TITLE_FORE_COLORS
                                    = { Color.magenta, Color.pink,
                                        Color.green, Color.yellow };
    private static final Color[]    COLUMN_TITLE_BACK_COLORS
                                    = { Color.black, Color.gray,
                                        Color.darkGray.darker(),
                                        Color.blue.darker() };
    private static final boolean[]  COLUMN_TITLE_RAISED_ICONS
                                    = { false, false, true, false };
    private static final Object[]   COLUMN_SAMPLES
                                    = { CategoryIcon.BLANK_ICON,
                                        "ABCDEFGHIJKLMNOP",
                                        Boolean.TRUE, Boolean.TRUE };

    private List   objdef_list    = null;
    private List   icon_list      = null;

    public LegendTableModel( CategoryMap map )
    {
        super();

        objdef_list  = new ArrayList( map.values() );
        this.sortNormally( LegendComparators.CASE_SENSITIVE_ORDER );
    }



    //  Sorting into various order
    private void initIconListFromCategoryList()
    {
        icon_list    = new ArrayList( objdef_list.size() );

        Icon      icon;
        Category  objdef;
        Iterator  objdefs;
        objdefs  = objdef_list.iterator();
        while ( objdefs.hasNext() ) {
            objdef = (Category) objdefs.next();
            icon   = new CategoryIcon( objdef );
            icon_list.add( icon );
        }
    }

    private void sortNormally( Comparator comparator )
    {
        Collections.sort( objdef_list, comparator );
        this.initIconListFromCategoryList();        
    }

    private void sortReversely( Comparator comparator )
    {
        Collections.sort( objdef_list, comparator );
        Collections.reverse( objdef_list );
        this.initIconListFromCategoryList();        
    }

    public void arrangeOrder( Comparator comparator )
    {
        this.sortNormally( comparator );
        super.fireTableDataChanged(); 
    }

    public void reverseOrder( Comparator comparator )
    {
        this.sortReversely( comparator );
        super.fireTableDataChanged(); 
    }



    public int getRowCount()
    {
        return objdef_list.size();
    }

    public int getColumnCount()
    {
        return COLUMN_TITLES.length;
    }

    public Class getColumnClass( int icolumn )
    {
        return COLUMN_CLASSES[ icolumn ];
    }

    public String getColumnName( int icolumn )
    {
        return COLUMN_TITLES[ icolumn ];
    }

    public Color getColumnNameForeground( int icolumn )
    {
        return COLUMN_TITLE_FORE_COLORS[ icolumn ];
    }

    public Color getColumnNameBackground( int icolumn )
    {
        return COLUMN_TITLE_BACK_COLORS[ icolumn ];
    }

    public boolean isRaisedColumnNameIcon( int icolumn )
    {
        return COLUMN_TITLE_RAISED_ICONS[ icolumn ];
    }

    public String getColumnToolTip( int icolumn )
    {
        return COLUMN_TOOLTIPS[ icolumn ];
    }

    public Object getColumnTypicalValue( int icolumn )
    {
        return COLUMN_SAMPLES[ icolumn ];
    }

    public Object getValueAt( int irow, int icolumn )
    {
        Category  objdef;
        switch ( icolumn ) {
            case ICON_COLUMN :
                return icon_list.get( irow );
            case NAME_COLUMN :
                objdef = (Category) objdef_list.get( irow );
                return objdef.getName();
            case VISIBILITY_COLUMN :
                objdef = (Category) objdef_list.get( irow );
                if ( objdef.isVisible() )
                    return Boolean.TRUE;
                else
                    return Boolean.FALSE;
            case SEARCHABILITY_COLUMN :
                objdef = (Category) objdef_list.get( irow );
                if ( objdef.isSearchable() )
                    return Boolean.TRUE;
                else
                    return Boolean.FALSE;
            default:
                System.err.println( "LegendTableModel.getValueAt("
                                  + irow + "," + icolumn + ") fails!" );
                return null;
        }
    }

    public boolean isCellEditable( int irow, int icolumn )
    {
        return true;
    }

    public void setValueAt( Object value, int irow, int icolumn )
    {
        Category      objdef;
        CategoryIcon  icon;
        ColorAlpha    color;

        objdef = (Category) objdef_list.get( irow );
        switch ( icolumn ) {
            case ICON_COLUMN :
                color  = (ColorAlpha) value;
                objdef.setColor( color );
                icon   = (CategoryIcon) icon_list.get( irow );
                icon.setDisplayedColor( color );
                fireTableCellUpdated( irow, icolumn );
                break;
            case NAME_COLUMN :
                objdef.setName( (String) value );
                fireTableCellUpdated( irow, icolumn );
                break;
            case VISIBILITY_COLUMN :
                objdef.setVisible( ( (Boolean) value ).booleanValue() );
                fireTableCellUpdated( irow, icolumn );
                break;
            case SEARCHABILITY_COLUMN :
                objdef.setSearchable( ( (Boolean) value ).booleanValue() );
                fireTableCellUpdated( irow, icolumn );
                break;
            default:
                System.err.print( "LegendTableModel.setValueAt("
                                + irow + "," + icolumn + ") fails!" );
        }
        // System.out.println( "setValueAt("+irow+","+icolumn+"): "+objdef );
    }

/*
    public void valueChanged( ListSelectionEvent evt ) 
    {
        if ( evt.getValueIsAdjusting() )
            return;

        ListSelectionModel  select_model;
        int                 min_idx, max_idx;
        select_model = (ListSelectionModel) evt.getSource();
        min_idx      = select_model.getMinSelectionIndex();
        max_idx      = select_model.getMaxSelectionIndex();
        for ( int irow = min_idx; irow <= max_idx; irow++ )
             if ( select_model.isSelectedIndex( irow ) )
                 System.out.println( "ListSelectionListener.valueChanged("
                                   + irow + ")" );
    }
*/
}

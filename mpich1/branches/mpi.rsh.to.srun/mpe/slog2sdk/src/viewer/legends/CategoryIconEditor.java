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
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.JTable;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JColorChooser;
import javax.swing.DefaultCellEditor;
import javax.swing.table.TableCellEditor;

import base.drawable.ColorAlpha;

// Used DefaultCellEditor instead of AbstractCellEditor so jre1.2.2 can be used
// public class CategoryIconEditor extends AbstractCellEditor
public class CategoryIconEditor extends DefaultCellEditor
                                implements TableCellEditor,
                                           ActionListener
{
    private JButton      delegate_btn;
    private ColorAlpha   saved_color;
    private Color        prev_color;

    public CategoryIconEditor()
    {
        super( new JCheckBox() );         // super(); for DefaultCellEditor
        delegate_btn  = new JButton();
        delegate_btn.addActionListener( this );
        editorComponent = delegate_btn;   // for DefaultCellEditor
        super.setClickCountToStart(1);    // for DefaultCellEditor
        saved_color   = null;
        prev_color    = null;
    }

    // Called 1st
    public Component getTableCellEditorComponent( JTable   table,
                                                  Object   value,
                                                  boolean  isSelected,
                                                  int      irow,
                                                  int      icolumn )
    {
        // save color in "(CategoryIcon) value" for setting JColorChooser later
        CategoryIcon icon;
        icon        = (CategoryIcon) value;
        prev_color  = icon.getDisplayedColor();
        delegate_btn.setIcon( icon );
        return delegate_btn;
    }

    // Called 2nd
    public void actionPerformed( ActionEvent evt )
    {
        Color new_color = JColorChooser.showDialog( delegate_btn,
                                                    "Pick a Color",
                                                    prev_color );
        if ( new_color != null ) 
            saved_color = new ColorAlpha( new_color, ColorAlpha.OPAQUE );
        else
            saved_color = new ColorAlpha( prev_color, ColorAlpha.OPAQUE );
        fireEditingStopped();
    }

    // Called 3rd
    public Object getCellEditorValue()
    {
        return saved_color;
    }
}

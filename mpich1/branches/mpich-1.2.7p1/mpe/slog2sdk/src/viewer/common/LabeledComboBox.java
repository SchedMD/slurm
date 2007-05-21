/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import java.awt.Font;
import java.awt.FontMetrics;
import java.awt.Dimension;
import java.awt.Component;
import java.awt.event.ItemEvent;
import java.awt.event.ItemListener;
import java.awt.event.ActionListener;
import javax.swing.*;

public class LabeledComboBox extends JPanel
{
    private static final   int     TEXT_HEIGHT = 20;
    private static         Font    FONT        = null;

    private JLabel                 tag;
    private JComboBox              lst;

    public LabeledComboBox( String label )
    {
        super();
        super.setLayout( new BoxLayout( this, BoxLayout.Y_AXIS ) );
        JPanel tag_panel = new JPanel();
        tag_panel.setLayout( new BoxLayout( tag_panel, BoxLayout.X_AXIS ) );
            tag = new JLabel( label );
        tag_panel.add( Box.createHorizontalStrut( Const.LABEL_INDENTATION ) );
        tag_panel.add( tag );
        tag_panel.add( Box.createHorizontalGlue() );

        lst = new JComboBox();
        tag.setLabelFor( lst );
        tag_panel.setAlignmentX( Component.LEFT_ALIGNMENT );
        lst.setAlignmentX( Component.LEFT_ALIGNMENT );

        super.add( tag_panel );
        super.add( lst );

        // tag.setBorder( BorderFactory.createEtchedBorder() );
        lst.setBorder( BorderFactory.createLoweredBevelBorder() );

        if ( FONT != null ) {
            tag.setFont( FONT );
            lst.setFont( FONT );
        }
    }

    public static void setDefaultFont( Font font )
    {
        FONT = font;
    }

    public void setLabelFont( Font font )
    {
        tag.setFont( font );
    }

    public void setFieldFont( Font font )
    {
        lst.setFont( font );
    }

    public void setEditable( boolean flag )
    {
        lst.setEditable( flag );
    }

    public void setEnabled( boolean flag )
    {
        lst.setEnabled( flag );
    }

    public void addItem( Object new_item )
    {
        lst.addItem( new_item );
    }

    public void setSelectedItem( Object an_object )
    {
        lst.setSelectedItem( an_object );
    }

    public void setSelectedBooleanItem( boolean bool_val )
    {
        if ( bool_val )
            lst.setSelectedItem( Boolean.TRUE );
        else
            lst.setSelectedItem( Boolean.FALSE );
    }

    public Object getSelectedItem()
    {
        return lst.getSelectedItem();
    }

    public boolean getSelectedBooleanItem()
    {
        return ((Boolean) lst.getSelectedItem()).booleanValue();
    }

    public void addActionListener( ActionListener listener )
    {
        lst.addActionListener( listener );
    }

    // BoxLayout respects component's maximum size
    public Dimension getMaximumSize()
    {
        return new Dimension( Short.MAX_VALUE,
                              lst.getPreferredSize().height
                            + this.TEXT_HEIGHT );
    }
}

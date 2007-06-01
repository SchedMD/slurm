/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.Component;
import javax.swing.JLabel;
import javax.swing.JSlider;
import javax.swing.BorderFactory;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;
import java.util.Hashtable;


public class LabeledFloatSlider extends LabeledTextField
                                implements ChangeListener,
                                           ActionListener
{
    private static final  int   SLIDER_MIN    = 0;
    private static final  int   SLIDER_MAX    = 10000;
    private static final  int   SLIDER_EXTENT = SLIDER_MAX - SLIDER_MIN;

    private JSlider  slider;
    private float    fmin;
    private float    fmax;
    private float    fextent;

    public LabeledFloatSlider( String label, float min_label, float max_label )
    {
        super( true, label, Const.FLOAT_FORMAT );
        fmin     = min_label;
        fmax     = max_label;
        fextent  = fmax - fmin;

        JLabel    tick_mark;
        Hashtable label_table;
        int       ival;
        float     fval;

        ival    = (SLIDER_MIN + SLIDER_MAX) / 2;
        slider  = new JSlider( JSlider.HORIZONTAL,
                               SLIDER_MIN, SLIDER_MAX, ival );       
        slider.setPaintLabels( true );
        fval    = ivalue2flabel( ival );
        label_table = new Hashtable();
            tick_mark = new JLabel( fmt.format(fmin) );
            if ( FONT != null )
                tick_mark.setFont( FONT );
        label_table.put( new Integer( SLIDER_MIN ), tick_mark );
            tick_mark = new JLabel( fmt.format(fval) );
            if ( FONT != null )
                tick_mark.setFont( FONT );
        label_table.put( new Integer( ival ), tick_mark );
            tick_mark = new JLabel( fmt.format(fmax) );
            if ( FONT != null )
                tick_mark.setFont( FONT );
        label_table.put( new Integer( SLIDER_MAX ), tick_mark );
        slider.setLabelTable( label_table );
        slider.setBorder( BorderFactory.createLoweredBevelBorder() );
        slider.setAlignmentX( Component.LEFT_ALIGNMENT );
        slider.addChangeListener( this );
        super.addActionListener( this );
        super.add( slider );
    }

    private float  ivalue2flabel( int ival )
    {
        return fextent / SLIDER_EXTENT * ((float)( ival - SLIDER_MIN )) + fmin;
    }

    private int    flabel2ivalue( float fval )
    {
        return  (int) Math.round( (float) SLIDER_EXTENT / fextent
                                * ( fval - fmin ) ) + SLIDER_MIN;
    }

    public void setFloat( float fval )
    {
        int   ival;
        ival  = flabel2ivalue( fval );
        if ( ival <= SLIDER_MIN ) {
            ival = SLIDER_MIN + 1;
            fval = ivalue2flabel( ival );
        }
        if ( ival >= SLIDER_MAX ) {
            ival = SLIDER_MAX - 1;
            fval = ivalue2flabel( ival );
        }
        super.setFloat( fval );
        slider.setValue( ival );
    }

    public void stateChanged( ChangeEvent evt )
    {
        int   ival;
        float fval;
        ival  = slider.getValue();
        if ( ival <= SLIDER_MIN ) {
            ival = SLIDER_MIN + 1;
            slider.setValue( ival );
        }
        if ( ival >= SLIDER_MAX ) {
            ival = SLIDER_MAX - 1;
            slider.setValue( ival );
        }
        fval  = ivalue2flabel( ival );
        super.setFloat( fval );
    }

    public void actionPerformed( ActionEvent evt )
    {
        int   ival;
        float fval;
        fval  = super.getFloat();
        ival  = flabel2ivalue( fval );
        if ( ival <= SLIDER_MIN ) {
            ival = SLIDER_MIN + 1;
            super.setFloat( ivalue2flabel( ival ) );
        }
        if ( ival >= SLIDER_MAX ) {
            ival = SLIDER_MAX - 1;
            super.setFloat( ivalue2flabel( ival ) );
        }
        slider.setValue( ival );
    }
}

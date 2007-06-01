/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.Dictionary;
import java.util.Hashtable;
import java.util.Enumeration;
import java.text.NumberFormat;
import java.text.DecimalFormat;

import java.awt.event.MouseEvent;
import java.awt.event.MouseListener;
import java.awt.event.MouseAdapter;

import java.awt.Font;
import java.awt.Color;
import javax.swing.JSlider;
import javax.swing.JLabel;
import javax.swing.JComponent;
import javax.swing.BorderFactory;

import viewer.common.Const;

public class ScaledSlider extends JSlider
{
    private static final Font      FONT        = Const.FONT;
    private static final  int      MODEL_MIN   = 0;
    private static final  int      MODEL_MAX   = 1000000000;
    // private static final  int      MODEL_MAX   = 1073741824;  // = 2^(30)

    private double        label_min;
    private double        label_max;

    private double        ratio_label2model;
    private Font          label_font;
    private DecimalFormat label_fmt;

    public ScaledSlider( int orientation )
    {
        super( orientation, MODEL_MIN, MODEL_MAX, MODEL_MIN );
        // this.putClientProperty( "JSlider.isFilled", Boolean.TRUE );
        super.setBackground( Color.white );
        super.setPaintTrack( true );
        super.setPaintLabels( true );
        super.setPaintTicks( true );
        super.setFont( FONT );
        super.setBorder( BorderFactory.createLoweredBevelBorder() );

        label_min  = 0.0d;
        label_max  = 100.0d;
        label_font = FONT;
        setLabelFormat( "###0.0##" );

        /*
        super.addMouseListener( new MouseAdapter() {
            public void mouseClicked( MouseEvent evt ) {
                System.out.println( "MouseClicked at "
                                  + evt.getSource().getClass() );
            }
            public void mousePressed( MouseEvent evt ) {
                System.out.println( "MousePressed at "
                                  + evt.getSource().getClass() );
            }
            public void mouseReleased( MouseEvent evt ) {
                System.out.println( "MouseReleased at "
                                  + evt.getSource().getClass() );
            }
        } );
        */
    }

    public void setLabelFormat( String format )
    {
        if ( format != null ) {
            label_fmt = (DecimalFormat) NumberFormat.getInstance();
            label_fmt.applyPattern( format );
        } 
    }


    /*
        label_val - label_min     label_max - label_min
        ---------------------  =  ---------------------
        model_val - MODEL_MIN     MODEL_MAX - MODEL_MIN
        
    */
    private double getModel2LabelValue( int model_val )
    {
        return ratio_label2model * ( model_val - MODEL_MIN ) + label_min;
    }

    private int    getLabel2ModelValue( double label_val )
    {
        double fmodel_val = ( label_val - label_min ) / ratio_label2model
                          + MODEL_MIN;
        return (int) Math.round( fmodel_val );
    }

    private double getModel2LabelInterval( int model_intvl )
    {
        return ratio_label2model * model_intvl;
    }

    private int    getLabel2ModelInterval( double label_intvl )
    {
        return (int) Math.round( label_intvl / ratio_label2model );
    }



    private static double getMajorTickIntervalLabel( double a_max_label )
    {
        if ( a_max_label < 11.0d )
            return 1.0d;
        else if ( a_max_label < 22.0d )
            return 2.0d;
        else if ( a_max_label < 55.0d )
            return 5.0d;
        else if ( a_max_label < 110.0d )
            return 10.0d;
        else if ( a_max_label < 220.0d )
            return 20.0d;
        else if ( a_max_label < 440.0d )
            return 40.0d;
        else if ( a_max_label < 550.0d )
            return 50.0d;
        else
            return 100.0d;
    }

    public void setLabelFont( Font font )
    {
        label_font = font;
        if ( super.getPaintLabels() ) {
            Enumeration labels;
            JComponent  label;
            Dictionary  label_table;
            label_table = super.getLabelTable();
            if ( label_table != null ) {
                labels = super.getLabelTable().elements();
                while ( labels.hasMoreElements() ) {
                    label  = (JComponent) labels.nextElement();
                    label.setFont( label_font );
                }
            }
        }
    }

    private void setDefaultLabelTable()
    {
        Hashtable  label_table;
        JLabel     tick_mark;
        double     label_intvl, flabel;
        int        model_intvl, imodel;

        if ( label_fmt == null || !super.getPaintLabels() )
            return;

        label_intvl = getMajorTickIntervalLabel( label_max );
        model_intvl = this.getLabel2ModelInterval( label_intvl );
        super.setMajorTickSpacing( model_intvl );
        label_table = new Hashtable();
        for ( imodel = MODEL_MIN; imodel < MODEL_MAX; imodel+=model_intvl ) {
            flabel = getModel2LabelValue( imodel );
            tick_mark = new JLabel( label_fmt.format( flabel ) );
            tick_mark.setFont( label_font );
            label_table.put( new Integer( imodel ), tick_mark ); 
            // System.out.println( "label_table.put(" + imodel + ","
            //                   + tick_mark.getText() + ")" );
        }
        super.setLabelTable( label_table );
    }

    public void setMaxLabel( double a_max_label )
    {
        label_max  = a_max_label;
        ratio_label2model = ( label_max - label_min )
                          / ( MODEL_MAX - MODEL_MIN );
        this.setDefaultLabelTable();
    }

    public void setMaxLabel( int i_max_label )
    { this.setMaxLabel( (double) i_max_label ); }

    public double getMaxLabel()
    {
        return label_max;
    }

    public void setMinLabel( double a_min_label )
    {
        label_min  = a_min_label;
        ratio_label2model = ( label_max - label_min )
                          / ( MODEL_MAX - MODEL_MIN );
        this.setDefaultLabelTable();
    }

    public void setMinLabel( int i_min_label )
    { this.setMinLabel( (double) i_min_label ); }

    public double getMinLabel()
    {
        return label_min;
    }



    // This should trigger the ChangeListener registed to this component
    public void setValLabel( double a_val_label )
    {
        double   label_value  = a_val_label;
        if ( label_value > label_max ) {
            System.err.println( "label_value(" + label_value
                              + ") > label_max(" + label_max + ")" );
            this.setMaxLabel( label_value );
        }
        else if ( label_value < label_min ) {
            System.err.println( "label_value(" + label_value
                              + ") < label_min(" + label_min + ")" );
            this.setMinLabel( label_value );
        }
        int  model_value = this.getLabel2ModelValue( label_value );

        if ( model_value > MODEL_MAX )
            System.err.println( "model_value(" + model_value
                              + ") > MODEL_MAX(" + MODEL_MAX + ")" );
        else if ( model_value < MODEL_MIN )
            System.err.println( "model_value(" + model_value
                              + ") < MODEL_MIN(" + MODEL_MIN + ")" );
        // System.out.println( "model_value = " + model_value );

        super.setValue( model_value );
    }

    public double getValLabel()
    {
        return this.getModel2LabelValue( super.getValue() );
    }

    public void setValLabelFully( double a_val_label )
    { 
        this.setValLabel( a_val_label );
        // This seems to cause an infinite recursive loop.
        // this.simulateMouseClicked();
    }

    private MouseEvent  simulated_click = null;

    private void simulateMouseClicked()
    {
        MouseListener[]  listeners;
        MouseListener    listener;
        int              idx;

        if ( simulated_click == null )
            // simulated_click = new MouseEvent( this, MouseEvent.BUTTON1,
            simulated_click = new MouseEvent( this, MouseEvent.MOUSE_CLICKED,
                                              System.currentTimeMillis(),
                                              0, 5, 5, 1, false );
        listeners = (MouseListener[]) this.getListeners( MouseListener.class );
        for ( idx = 0; idx < listeners.length; idx++ ) {
             listener  = listeners[ idx ];
             listener.mousePressed( simulated_click );
             listener.mouseReleased( simulated_click );
        }
    }

    public void fireStateChanged()
    {
        super.fireStateChanged();
    }
}

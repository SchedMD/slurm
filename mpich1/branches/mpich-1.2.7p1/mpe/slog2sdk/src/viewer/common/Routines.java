/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import java.awt.*;
import java.awt.image.BufferedImage;
import java.net.URL;
import javax.swing.ImageIcon;
import javax.swing.JComponent;

public class Routines
{
    private static final String UnitIndentStr = "   ";
    public static void listAllComponents( Component comp, int ilevel )
    {
        if ( comp == null ) return;
        StringBuffer rep = new StringBuffer();
        for ( int ii = 0; ii < ilevel; ii++ )
            rep.append( UnitIndentStr );
        rep.append( comp.toString() );
        System.out.println( rep.toString() );
        if ( comp instanceof Container ) {
            Component [] child_comps = ( (Container) comp ).getComponents();
            for ( int idx = 0; idx < child_comps.length; idx++)
                listAllComponents( child_comps[ idx ], ilevel+1 );
        }
    }

    public static void setComponentAndChildrenCursors( Component  comp,
                                                       Cursor     csr )
    {
        if ( comp == null )
            return;
        comp.setCursor( csr );
        if ( comp instanceof Container ) {
            Component [] comps = ( (Container) comp ).getComponents();
            for ( int ii = 0; ii < comps.length; ii++)
                setComponentAndChildrenCursors( comps[ ii ], csr );
        }
    }

    public static void setShortJComponentSizes( JComponent  comp,
                                                Dimension   pref_size )
    {
        Dimension  min_size, max_size;
        min_size  = new Dimension( 0, pref_size.height );
        max_size  = new Dimension( Short.MAX_VALUE, pref_size.height );
        comp.setMinimumSize( min_size );
        comp.setMaximumSize( max_size );
        comp.setPreferredSize( pref_size );
    }



    public static Dimension getScreenSize()
    {
        return Toolkit.getDefaultToolkit().getScreenSize();
    }

    public static Dimension correctSize( Dimension size, final Insets insets )
    {
        if ( insets != null ) {
            size.width  += insets.left + insets.right;
            if ( size.width > Short.MAX_VALUE )
                size.width  = Short.MAX_VALUE;
            size.height += insets.top + insets.bottom;
            if ( size.height > Short.MAX_VALUE )
                size.height = Short.MAX_VALUE;
        }
        return size;
    }

    //  JTextField.getColumnWidth() uses char('m') defines column width
    //  getAdjNumOfTextColumns() computes the effective char column number
    //  that is needed by the JTextField's setColumns().
    //  This routine should be good for both JTextField and JTextArea
    public static int getAdjNumOfTextColumns( Component textcomp,
                                              int num_numeric_columns )
    {
        FontMetrics metrics;
        int         num_char_columns;

        metrics = textcomp.getFontMetrics( textcomp.getFont() );
        num_char_columns = (int) Math.ceil( (double) num_numeric_columns
                                          * metrics.charWidth( '1' )
                                          / metrics.charWidth( 'm' ) );
        // System.out.println( "num_char_columns = " + num_char_columns );
        return num_char_columns;
    }

    private static final  double  MATH_LOG_10 = Math.log( 10.0 );
    /*
       getRulerIncrement() takes in an estimated time increment and
       returns a more appropriate time increment
    */
    public static double getTimeRulerIncrement( double t_incre )
    {
        double incre, incre_expo, incre_ftr, tmp_mant, incre_mant;
        incre      = t_incre;
        incre_expo = Math.ceil( Math.log( incre ) / MATH_LOG_10 );
        incre_ftr  = Math.pow( 10.0, incre_expo );
        tmp_mant   = incre / incre_ftr;
        if ( tmp_mant < 0.1125 )
            incre_mant = 0.1;
        else if ( tmp_mant < 0.1625 )
            incre_mant = 0.125;
        else if ( tmp_mant < 0.225 )
            incre_mant = 0.2;
        else if ( tmp_mant < 0.325 )
            incre_mant = 0.25;
        else if ( tmp_mant < 0.45 )
            incre_mant = 0.4;
        else if ( tmp_mant < 0.75 )
            incre_mant = 0.5;
        else
            incre_mant = 1.0;

        // system.err.println( "Routines.getTimeRulerIncrement("
        //                   + t_incre + ") = " + incre_mant * incre_ftr );
        return incre_mant * incre_ftr;
    }

    public static double getTimeRulerFirstMark( double t_init, double t_incre )
    {
        double quotient;
        // quotient = Math.ceil( t_init / t_incre );
        quotient = Math.floor( t_init / t_incre );
        return quotient * t_incre;
    }



    private static final double COLOR_FACTOR = 0.85;

    public static Color getSlightBrighterColor( Color color )
    {
        int red   = color.getRed();
        int green = color.getGreen();
        int blue  = color.getBlue();

        /* From 2D group:
         * 1. black.brighter() should return grey
         * 2. applying brighter to blue will always return blue, brighter
         * 3. non pure color (non zero rgb) will eventually return white
         */
        int ii = (int) ( 1.0 / (1.0-COLOR_FACTOR) );
        if ( red == 0 && green == 0 && blue == 0)
           return new Color( ii, ii, ii );

        if ( red > 0 && red < ii )
            red = ii;
        if ( green > 0 && green < ii )
            green = ii;
        if ( blue > 0 && blue < ii )
            blue = ii;

        return new Color( Math.min( (int)(red  /COLOR_FACTOR), 255 ),
                          Math.min( (int)(green/COLOR_FACTOR), 255 ),
                          Math.min( (int)(blue /COLOR_FACTOR), 255 ) );
    }

    public static Color getSlightDarkerColor( Color color )
    {
        return new Color( Math.max( (int)(color.getRed()  *COLOR_FACTOR), 0),
                          Math.max( (int)(color.getGreen()*COLOR_FACTOR), 0),
                          Math.max( (int)(color.getBlue() *COLOR_FACTOR), 0) );
    }
}

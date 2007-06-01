/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.text.NumberFormat;
import java.text.DecimalFormat;
import java.text.ChoiceFormat;

public class TimeFormat
{
    private static final double[] LIMITS  = {Double.NEGATIVE_INFINITY, 0.0d,
                                             0.1E-9, 0.1E-6, 0.1E-3, 0.1d};
    private static final String[] UNITS   = {"-ve", "ps", "ns",
                                             "us", "ms", "s" };
    private static final String   PATTERN = "#,##0.00###";

    private              DecimalFormat decfmt   = null;
    private              ChoiceFormat  unitfmt  = null;

    public TimeFormat()
    {
        decfmt = (DecimalFormat) NumberFormat.getInstance();
        decfmt.applyPattern( PATTERN );
        unitfmt = new ChoiceFormat( LIMITS, UNITS );
    }

    public String format( double time )
    {
        String unit = unitfmt.format( Math.abs( time ) );
        if ( unit.equals( "s" ) )
            return decfmt.format(time) + " sec";
        else if ( unit.equals( "ms" ) )
            return decfmt.format(time * 1.0E3) + " msec";
        else if ( unit.equals( "us" ) )
            return decfmt.format(time * 1.0E6) + " usec";
        else if ( unit.equals( "ns" ) )
            return decfmt.format(time * 1.0E9) + " nsec";
        else if ( unit.equals( "ps" ) )
            return decfmt.format(time * 1.0E12) + " psec";
        else
            return decfmt.format(time) + " sec";
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.text.NumberFormat;
import java.text.DecimalFormat;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.JTextField;
import javax.swing.JProgressBar;
import java.io.File;

public class ProgressAction implements ActionListener
{
    public  static final int            MIN_PROGRESS_VALUE  = 0;
    public  static final int            MAX_PROGRESS_VALUE  = 100;

    private static final double         KILOBYTE            = 1024.0d;
    private static       DecimalFormat  fmt                 = null;
    
    private JTextField    text_fld;
    private JProgressBar  progress_bar;
    private File          infile, outfile;
    private long          infile_size, outfile_size;

    public ProgressAction( JTextField    a_text_fld,
                           JProgressBar  a_progress_bar )
    {
        text_fld      = a_text_fld;
        if ( text_fld != null ) {
            fmt = (DecimalFormat) NumberFormat.getInstance();
            fmt.applyPattern( "#,###,###,##0.0" );
            text_fld.setColumns( 18 );
            text_fld.setHorizontalAlignment( JTextField.RIGHT );
        }

        progress_bar  = a_progress_bar;
        if ( progress_bar != null ) {
            progress_bar.setMinimum( MIN_PROGRESS_VALUE );
            progress_bar.setMaximum( MAX_PROGRESS_VALUE );
        }

        infile        = null;
        outfile       = null;
        infile_size   = 0;
        outfile_size  = 0;
    }

    public void initialize( File input_file, File output_file )
    {
        infile       = input_file;
        outfile      = output_file;
        if ( text_fld != null )
            text_fld.setText( fmt.format( 0.0d ) + " KB" );
        if ( progress_bar != null ) {
            progress_bar.setValue( MIN_PROGRESS_VALUE );
            infile_size  = infile.length();
        }
    }

    // Cannot use finalize() as function name,
    // finalize() overrides Object.finalize().
    public void finish()
    {
        if ( progress_bar != null )
            progress_bar.setValue( MAX_PROGRESS_VALUE );
    }

    public void actionPerformed( ActionEvent evt )
    {
        int     progress_min, progress_max;
        double  progress_val;

        outfile_size  = outfile.length();

        if ( text_fld != null )
            text_fld.setText( fmt.format( (double) outfile_size / KILOBYTE )
                            + " KB" );
        
        if ( progress_bar != null ) {
            progress_min  = progress_bar.getMinimum();
            progress_max  = progress_bar.getMaximum();
            progress_val  = ( progress_max - progress_min )
                          * (double) outfile_size / infile_size
                          + progress_min;
            progress_bar.setValue( (int) progress_val );
        }
    }
}

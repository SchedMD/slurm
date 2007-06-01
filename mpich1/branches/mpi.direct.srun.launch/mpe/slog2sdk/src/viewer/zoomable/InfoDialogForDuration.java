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
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

import base.drawable.TimeBoundingBox;
import viewer.common.Const;
import viewer.common.Routines;

public class InfoDialogForDuration extends InfoDialog
{
    private static final String          FORMAT = Const.INFOBOX_TIME_FORMAT;
    private static       DecimalFormat   fmt    = null;
    private static       TimeFormat      tfmt   = null;

    private              TimeBoundingBox timebox;
    private              ScrollableObject scrollable;

    public InfoDialogForDuration( final Frame             frame,
                                  final TimeBoundingBox   times,
                                  final ScrollableObject  scrollobj )
    {
        super( frame, "Duration Info Box", times.getLatestTime() );
        timebox     = times;
        scrollable  = scrollobj;
        this.init();
    }

    public InfoDialogForDuration( final Dialog            dialog,
                                  final TimeBoundingBox   times,
                                  final ScrollableObject  scrollobj )
    {
        super( dialog, "Duration Info Box", times.getLatestTime() );
        timebox     = times;
        scrollable  = scrollobj;
        this.init();
    }

    private void init()
    {
        /* Define DecialFormat for the displayed time */
        if ( fmt == null ) {
            fmt = (DecimalFormat) NumberFormat.getInstance();
            fmt.applyPattern( FORMAT );
        }
        if ( tfmt == null )
            tfmt = new TimeFormat();
        
        Container root_panel = this.getContentPane();
        root_panel.setLayout( new BoxLayout( root_panel, BoxLayout.Y_AXIS ) );

            StringBuffer textbuf = new StringBuffer();
            int          num_cols = 0, num_rows = 3;

            StringBuffer linebuf = new StringBuffer();
            linebuf.append( "duration = "
                          + tfmt.format(timebox.getDuration()) );
            num_cols = linebuf.length();
            textbuf.append( linebuf.toString() + "\n" );

            linebuf = new StringBuffer();
            linebuf.append( "[0]: time = "
                          + fmt.format(timebox.getEarliestTime()) );
            if ( num_cols < linebuf.length() )
                num_cols = linebuf.length();
            textbuf.append( linebuf.toString() + "\n" );
            
            linebuf = new StringBuffer();
            linebuf.append( "[1]: time = "
                          + fmt.format(timebox.getLatestTime()) );
            if ( num_cols < linebuf.length() )
                num_cols = linebuf.length();
            textbuf.append( linebuf.toString() );

            JTextArea text_area = new JTextArea( textbuf.toString() );
            int adj_num_cols    = Routines.getAdjNumOfTextColumns( text_area,
                                                                   num_cols );
            text_area.setColumns( adj_num_cols );
            text_area.setRows( num_rows );
            text_area.setEditable( false );
            text_area.setLineWrap( true );
        JScrollPane scroller = new JScrollPane( text_area );
        scroller.setAlignmentX( Component.LEFT_ALIGNMENT );
        root_panel.add( scroller );

        if ( scrollable instanceof SummarizableView ) {
            SummarizableView  summarizable;
            JPanel           ops4d_panel;
            summarizable = (SummarizableView) scrollable;  // CanvasXXXXline
            ops4d_panel  = new OperationDurationPanel( timebox, summarizable );
            ops4d_panel.setAlignmentX( Component.LEFT_ALIGNMENT );
            root_panel.add( ops4d_panel );
        }

        root_panel.add( super.getCloseButtonPanel() );
    }

    public TimeBoundingBox getTimeBoundingBox()
    {
        return timebox;
    }
}

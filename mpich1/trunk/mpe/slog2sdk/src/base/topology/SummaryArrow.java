/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.topology;

import java.awt.Graphics2D;
import java.awt.Color;
import java.awt.Point;
import java.awt.Stroke;
import java.awt.BasicStroke;
import java.util.Arrays;

import base.drawable.CoordPixelXform;
import base.statistics.CategoryTimeBox;
import base.statistics.TimeAveBox;

public class SummaryArrow
{
    /*
        Draw a Line between 2 vertices
        (start_time, start_ypos) and (final_time, final_ypos)
        Asssume caller guarantees : start_time <= final_time
    */
    private static int  drawForward( Graphics2D g, Color color, Stroke stroke,
                                     CoordPixelXform    coord_xform,
                                     double start_time, float start_ypos,
                                     double final_time, float final_ypos )
    {
        int      iStart, jStart, iFinal, jFinal;
        iStart   = coord_xform.convertTimeToPixel( start_time );
        iFinal   = coord_xform.convertTimeToPixel( final_time );

        jStart   = coord_xform.convertRowToPixel( start_ypos );
        jFinal   = coord_xform.convertRowToPixel( final_ypos );

        boolean  isStartVtxInImg, isFinalVtxInImg;
        isStartVtxInImg = iStart > 0 ;
        isFinalVtxInImg = iFinal < coord_xform.getImageWidth();

        double slope = 0.0;
        if ( !isStartVtxInImg || !isFinalVtxInImg )
            if ( iStart != iFinal )
                slope = (double) ( jFinal - jStart ) / ( iFinal - iStart );
            else
                return 0; // because both vertices are NOT in this image

        int iHead, iTail, jHead, jTail;
        // jHead = slope * ( iHead - iStart ) + jStart
        if ( isStartVtxInImg ) {
            iHead = iStart;
            jHead = jStart;
        }
        else {
            iHead = 0;
            jHead = (int) Math.rint( jStart - slope * iStart );
        }

        // jTail = slope * ( iTail - iFinal ) + jFinal
        if ( isFinalVtxInImg ) {
            iTail = iFinal;
            jTail = jFinal;
        }
        else {
            iTail = coord_xform.getImageWidth();
            jTail = (int) Math.rint( jFinal + slope * ( iTail - iFinal ) );
        }

        Stroke orig_stroke = null;
        if ( stroke != null ) {
            orig_stroke = g.getStroke();
            g.setStroke( stroke );
        }

        // Draw the line
        g.setColor( color );
        g.drawLine( iHead, jHead, iTail, jTail );

        if ( stroke != null )
            g.setStroke( orig_stroke );

        return 1;
    }



    public static void setTimeBoundingBox( TimeAveBox  avebox,
                                           double      starttime,
                                           double      finaltime )
    {
        CategoryTimeBox[]  typeboxes;
        CategoryTimeBox    typebox;
        boolean            isInclusive;
        double             prev_time, interval, duration;
        int                idx;

        // exclusive ratio is alway zero for summary arrow
        typeboxes  = avebox.arrayOfCategoryTimeBoxes();
        Arrays.sort( typeboxes, CategoryTimeBox.INCL_RATIO_ORDER );

        isInclusive  = true;  // alway true for arrow
        duration     = finaltime - starttime;
        for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
            typebox   = typeboxes[ idx ];
            interval  = duration * typebox.getCategoryRatio( isInclusive );
            typebox.setEarliestTime( starttime );
            typebox.setLatestFromEarliest( interval );
        }
    }


    private static Stroke    Line_Stroke  = new BasicStroke( 3.0f );

    private static long      Arrow_Log_Base = 10;
    private static Stroke[]  Line_Strokes;
    static {
         Line_Strokes = new Stroke[ 10 ];
         for ( int idx = Line_Strokes.length-1; idx >=0 ; idx-- )
             Line_Strokes[ idx ] = new BasicStroke( (float) (idx+1) );
    }

    public static void setBaseOfLogOfObjectNumToArrowWidth( int new_log_base )
    {
        Arrow_Log_Base = (long) new_log_base;
    }

    private static  Stroke  getArrowStroke( double  fnum )
    {
        long inum;
        int  idx;

        inum  = Math.round( fnum );
        for ( idx = 0; idx < Line_Strokes.length; idx++ ) {
             inum /= Arrow_Log_Base;
             if ( inum == 0 )
                 break;
        }
        if ( idx < Line_Strokes.length )
            return Line_Strokes[ idx ];
        else
            return Line_Strokes[ Line_Strokes.length-1 ];
    }

    public  static int  draw( Graphics2D  g, TimeAveBox  avebox,
                              CoordPixelXform  coord_xform,
                              float start_ypos, float final_ypos )
    {
        CategoryTimeBox[]  typeboxes;
        CategoryTimeBox    typebox;
        Color              color;
        Stroke             arrow_stroke;
        double             head_time, tail_time;
        float              head_ypos, tail_ypos;
        double             slope, intercept;
        int                count, idx;

        head_ypos  = start_ypos;
        tail_ypos  = final_ypos;
        typeboxes  = avebox.arrayOfCategoryTimeBoxes();

        count        = 0;
        arrow_stroke = getArrowStroke( avebox.getAveNumOfRealObjects() );
        typebox      = typeboxes[ typeboxes.length-1 ];
        head_time    = typebox.getEarliestTime();
        tail_time    = typebox.getLatestTime();
        slope        = (double) ( tail_ypos - head_ypos )
                              / ( tail_time - head_time );
        intercept    = (double) head_ypos - slope * head_time;
        for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
            typebox      = typeboxes[ idx ];
            color        = typebox.getCategoryColor();
            head_time    = typebox.getEarliestTime();  // independent of idx
            tail_time    = typebox.getLatestTime();
            tail_ypos    = (float) ( slope * tail_time + intercept );
            count  += drawForward( g, color, arrow_stroke, coord_xform,
                                   head_time, head_ypos,
                                   tail_time, tail_ypos );
        }

        return count;
    }

    public static Object containsPixel( TimeAveBox  avebox,
                                        CoordPixelXform coord_xform, Point pt,
                                        float start_ypos, float final_ypos )
    {
        CategoryTimeBox[]  typeboxes;
        CategoryTimeBox    typebox;
        double             head_time, tail_time;
        float              head_ypos, tail_ypos;
        double             slope, intercept;
        int                idx;

        head_ypos  = start_ypos;
        tail_ypos  = final_ypos;
        typeboxes  = avebox.arrayOfCategoryTimeBoxes();

        typebox      = typeboxes[ typeboxes.length-1 ];
        head_time    = typebox.getEarliestTime();
        tail_time    = typebox.getLatestTime();
        slope        = (double) ( tail_ypos - head_ypos )
                              / ( tail_time - head_time );
        intercept    = (double) head_ypos - slope * head_time;
        for ( idx = 0; idx < typeboxes.length; idx++ ) {
            typebox      = typeboxes[ idx ];
            head_time    = typebox.getEarliestTime();
            tail_time    = typebox.getLatestTime();
            tail_ypos    = (float) ( slope * tail_time + intercept );
            if ( Line.containsPixel( coord_xform, pt,
                                     head_time, head_ypos,
                                     tail_time, tail_ypos ) )
                return avebox;
        }

        return null;
    }
}

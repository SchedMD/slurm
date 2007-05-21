/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.topology;

import java.awt.Graphics2D;
import java.awt.Insets;
import java.awt.Color;
import java.awt.Point;
import java.util.Arrays;

import base.drawable.CoordPixelXform;
import base.drawable.TimeBoundingBox;
import base.statistics.CategoryTimeBox;
import base.statistics.TimeAveBox;

public class SummaryState
{
    // private static StateBorder BorderStyle = StateBorder.COLOR_XOR_BORDER;
    private static StateBorder BorderStyle  = StateBorder.WHITE_RAISED_BORDER;
    private static StateBorder BackBorder   = StateBorder.COLOR_XOR_BORDER;
    private static Color       BackColor    = Color.black;
    public  static Color       ForeColor    = Color.white;

    public static void setBorderStyle( final StateBorder state_border )
    { BorderStyle = state_border; }

    public static void setBackgroundColor( Color color )
    {
        BackColor   = color;
        if ( BackColor == Color.black )
            ForeColor = Color.lightGray;
        else if ( BackColor == Color.white )
            ForeColor = Color.darkGray;
        else if ( BackColor == Color.darkGray )
            ForeColor = Color.white;
        else if ( BackColor == Color.lightGray )
            ForeColor = Color.darkGray;
        else
            ForeColor = Color.white;
    }

    // The constant String's should be the same as those in PreviewState
    public  static final String FIT_MOST_LEGENDS
                                = PreviewState.FIT_MOST_LEGENDS;
    private static final int    FIT_MOST_LEGENDS_ID     = 0;
    public  static final String OVERLAP_INCLUSION
                                = PreviewState.OVERLAP_INCLUSION;
    private static final int    OVERLAP_INCLUSION_ID    = 1;
    public  static final String OVERLAP_EXCLUSION
                                = PreviewState.OVERLAP_EXCLUSION;
    private static final int    OVERLAP_EXCLUSION_ID    = 3;
    public  static final String CUMULATIVE_EXCLUSION
                                = PreviewState.CUMULATIVE_EXCLUSION;
    private static final int    CUMULATIVE_EXCLUSION_ID = 4;

    private static       int    DisplayType             = OVERLAP_INCLUSION_ID;

    public static void setDisplayType( String new_display_type )
    {
        if ( new_display_type.equals( FIT_MOST_LEGENDS ) )
            DisplayType = FIT_MOST_LEGENDS_ID;
        else if ( new_display_type.equals( OVERLAP_INCLUSION ) )
            DisplayType = OVERLAP_INCLUSION_ID;
        else if ( new_display_type.equals( OVERLAP_EXCLUSION ) )
            DisplayType = OVERLAP_EXCLUSION_ID;
        else if ( new_display_type.equals( CUMULATIVE_EXCLUSION ) )
            DisplayType = CUMULATIVE_EXCLUSION_ID;
        else
            DisplayType = OVERLAP_INCLUSION_ID;
    }

    public static boolean isDisplayTypeEqualWeighted()
    {
        return DisplayType == FIT_MOST_LEGENDS_ID;
    }

    public static boolean isDisplayTypeExclusiveRatio()
    {
        return    DisplayType == OVERLAP_EXCLUSION_ID
               || DisplayType == CUMULATIVE_EXCLUSION_ID;
    }

    public static boolean isDisplayTypeCumulative()
    {
        return    DisplayType == CUMULATIVE_EXCLUSION_ID;
    }

    private static        int    MinCategoryHeight          = 2;  
    private static        int    MinCategorySeparation      = 4;  

    public static void setMinCategoryHeight( int new_min_category_height )
    {
        MinCategoryHeight  = new_min_category_height;
    }

    /*
        Draw a Rectangle between left-upper vertex (start_time, start_ypos) 
        and right-lower vertex (final_time, final_ypos)
        Assume caller guarantees the order of timestamps and ypos, such that
        start_time <= final_time  and  start_ypos <= final_ypos.
    */
    private static int  drawForward( Graphics2D g, Color color,
                                     CoordPixelXform coord_xform,
                                     double start_time, float start_ypos,
                                     double final_time, float final_ypos )
    {
        int      iStart, jStart, iFinal, jFinal;
        iStart   = coord_xform.convertTimeToPixel( start_time );
        iFinal   = coord_xform.convertTimeToPixel( final_time );

        jStart   = coord_xform.convertRowToPixel( start_ypos );
        jFinal   = coord_xform.convertRowToPixel( final_ypos );

        boolean  isStartVtxInImg, isFinalVtxInImg;
        isStartVtxInImg = ( iStart >= 0 ) ;
        isFinalVtxInImg = ( iFinal <  coord_xform.getImageWidth() );

        int iHead, iTail, jHead, jTail;
        // jHead = slope * ( iHead - iStart ) + jStart
        if ( isStartVtxInImg )
            iHead = iStart;
        else
            iHead = 0;
            // iHead = -1;
        jHead    = jStart;

        // jTail = slope * ( iTail - iFinal ) + jFinal
        if ( isFinalVtxInImg )
            iTail = iFinal;
        else
            iTail = coord_xform.getImageWidth() - 1;
            // iTail = coord_xform.getImageWidth();
        jTail    = jFinal;

        if ( color == null )
            color = ForeColor;
      
        // Fill the color of the rectangle
        g.setColor( color );
        g.fillRect( iHead, jHead, iTail-iHead+1, jTail-jHead+1 );

        BorderStyle.paintStateBorder( g, color,
                                      iHead, jHead, isStartVtxInImg,
                                      iTail, jTail, isFinalVtxInImg );
        return 1;
    }

    /*
        Check if a point in pixel coordinate is in a Rectangle
        specified between left-upper vertex (start_time, start_ypos) 
        and right-lower vertex (final_time, final_ypos)
        Assume caller guarantees the order of timestamps and ypos, such that
        start_time <= final_time  and  start_ypos <= final_ypos

        Same as State.isPixelIn()
    */
    private static boolean isPixelIn( CoordPixelXform coord_xform, Point pt,
                                      double start_time, float start_ypos,
                                      double final_time, float final_ypos )
    {
        int      iStart, jStart, iFinal, jFinal;
        int      pt_x, pt_y;

        pt_y     = pt.y;

        jStart   = coord_xform.convertRowToPixel( start_ypos );
        if ( pt_y < jStart  )
            return false;

        jFinal   = coord_xform.convertRowToPixel( final_ypos );
        if ( pt_y > jFinal )
            return false;

        pt_x     = pt.x;

        iStart   = coord_xform.convertTimeToPixel( start_time );
        if ( pt_x < iStart )
            return false;

        iFinal   = coord_xform.convertTimeToPixel( final_time );
        if ( pt_x > iFinal )
            return false;

        return true;
    }



    public static void setTimeBoundingBox( TimeAveBox  avebox,
                                           double      starttime,
                                           double      finaltime )
    {
        CategoryTimeBox[]  typeboxes;
        CategoryTimeBox    typebox;
        TimeBoundingBox    curr_timebox;
        boolean            isInclusive;
        double             prev_time, interval, duration;
        int                vis_typeboxes_length, idx;

        typeboxes  = avebox.arrayOfCategoryTimeBoxes();
        if ( isDisplayTypeExclusiveRatio() )
            Arrays.sort( typeboxes, CategoryTimeBox.EXCL_RATIO_ORDER );
        else // OverlapInclusionRatio, CumulativeInclusionRatio, FitMostLegends
            Arrays.sort( typeboxes, CategoryTimeBox.INCL_RATIO_ORDER );

        /*
           CategoryTimeBox[] is in ascending order of the respective ratio
           set TimeBoundingBox of CategoryTimeBox[] in descending ratio order
        */

        curr_timebox  = avebox.getCurrentTimeBoundingBox();
        curr_timebox.reinitialize();
        if ( isDisplayTypeEqualWeighted() ) {
            vis_typeboxes_length = 0;
            for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
                 if ( typeboxes[ idx ].isCategoryVisiblySearchable() )
                     vis_typeboxes_length++ ;
            }
            prev_time  = starttime;
            interval   = ( finaltime - starttime ) / vis_typeboxes_length;
            for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
                typebox   = typeboxes[ idx ];
                if ( typebox.isCategoryVisiblySearchable() ) {
                    typebox.setEarliestTime( prev_time );
                    typebox.setLatestFromEarliest( interval );
                    prev_time = typebox.getLatestTime();
                    curr_timebox.affectTimeBounds( typebox );
                }
            }
        }
        else {
            isInclusive  = ! isDisplayTypeExclusiveRatio();
            if ( isDisplayTypeCumulative() ) { // CumulativeXXclusionRatio
                prev_time  = starttime;
                duration   = finaltime - starttime;
                for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
                    typebox   = typeboxes[ idx ];
                    if ( typebox.isCategoryVisiblySearchable() ) {
                        interval  = duration
                                  * typebox.getCategoryRatio( isInclusive );
                        typebox.setEarliestTime( prev_time );
                        typebox.setLatestFromEarliest( interval );
                        prev_time = typebox.getLatestTime();
                        curr_timebox.affectTimeBounds( typebox );
                    }
               }
            }
            else {  // OverlapInclusionRatio, OverlapExclusiveRatio
                duration   = finaltime - starttime;
                for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
                    typebox   = typeboxes[ idx ];
                    if ( typebox.isCategoryVisiblySearchable() ) {
                        interval  = duration
                                  * typebox.getCategoryRatio( isInclusive );
                        typebox.setEarliestTime( starttime );
                        typebox.setLatestFromEarliest( interval );
                        curr_timebox.affectTimeBounds( typebox );
                    }
               }
            }
        }
    }

    public  static int  draw( Graphics2D  g, TimeAveBox  avebox,
                              CoordPixelXform  coord_xform,
                              float  start_ypos, float  final_ypos,
                              float  avebox_height )
    {
        CategoryTimeBox[]  typeboxes;
        CategoryTimeBox    typebox;
        TimeBoundingBox    curr_timebox;
        Color              color;
        double             head_time, tail_time;
        float              head_ypos, tail_ypos, gap_ypos;
        int                count, idx;

        if ( start_ypos < final_ypos ) {
            head_ypos  = start_ypos;
            tail_ypos  = final_ypos;
        }
        else {
            head_ypos  = final_ypos;
            tail_ypos  = start_ypos;
        }

        // Draw CategoryTimeBox[] in descending ratio order
        count         = 0;
        curr_timebox  = avebox.getCurrentTimeBoundingBox();
        typeboxes     = avebox.arrayOfCategoryTimeBoxes();
        if (    isDisplayTypeEqualWeighted()
             || isDisplayTypeCumulative() ) {
            if (   head_ypos + avebox_height
                 < tail_ypos - avebox_height ) {
                head_time  = curr_timebox.getEarliestTime();
                tail_time  = curr_timebox.getLatestTime();
                count += drawForward( g, null, coord_xform,
                                      head_time, head_ypos,
                                      tail_time, tail_ypos );
                head_ypos += avebox_height;
                tail_ypos -= avebox_height;
            }
            for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
                typebox    = typeboxes[ idx ];
                color      = typebox.getCategoryColor();
                head_time  = typebox.getEarliestTime();
                tail_time  = typebox.getLatestTime();
                count += drawForward( g, color, coord_xform,
                                      head_time, head_ypos,
                                      tail_time, tail_ypos );
            } 
        }
        else { // OverlapXXclusionRatio
            if (   head_ypos + avebox_height
                 < tail_ypos - avebox_height ) {
                head_time  = curr_timebox.getEarliestTime();
                tail_time  = curr_timebox.getLatestTime();
                count += drawForward( g, null, coord_xform,
                                      head_time, head_ypos,
                                      tail_time, tail_ypos );
                head_ypos += avebox_height;
                tail_ypos -= avebox_height;
            }
            gap_ypos = ( tail_ypos - head_ypos ) / ( typeboxes.length * 2 );
            for ( idx = typeboxes.length-1; idx >= 0; idx-- ) {
                typebox    = typeboxes[ idx ];
                color      = typebox.getCategoryColor();
                head_time  = typebox.getEarliestTime();
                tail_time  = typebox.getLatestTime();
                count += drawForward( g, color, coord_xform,
                                      head_time, head_ypos,
                                      tail_time, tail_ypos );
                head_ypos += gap_ypos;
                tail_ypos -= gap_ypos;
            } 
        }
        return count;
    }

    public static Object containsPixel( TimeAveBox  avebox,
                                        CoordPixelXform coord_xform, Point pt,
                                        float start_ypos, float final_ypos,
                                        float avebox_height )
    {
        CategoryTimeBox[]  typeboxes;
        CategoryTimeBox    typebox;
        TimeBoundingBox    curr_timebox;
        double             head_time, tail_time;
        float              head_ypos, tail_ypos, gap_ypos;
        boolean            hasBoundary;
        int                idx;

        if ( start_ypos < final_ypos ) {
            head_ypos  = start_ypos;
            tail_ypos  = final_ypos;
        }
        else {
            head_ypos  = final_ypos;
            tail_ypos  = start_ypos;
        }

        if ( pt.y < coord_xform.convertRowToPixel( head_ypos ) )
            return null;

        if ( pt.y > coord_xform.convertRowToPixel( tail_ypos ) )
            return null;

        // Search CategoryTimeBox[] in ascending ratio order
        curr_timebox  = avebox.getCurrentTimeBoundingBox();
        typeboxes     = avebox.arrayOfCategoryTimeBoxes();
        if (    isDisplayTypeEqualWeighted()
             || isDisplayTypeCumulative() ) {
            if (   head_ypos + avebox_height
                 < tail_ypos - avebox_height ) {
                head_ypos  += avebox_height;
                tail_ypos  -= avebox_height;
                hasBoundary = true;
            }
            else
                hasBoundary = false;
            for ( idx = 0; idx < typeboxes.length; idx++ ) {
                typebox    = typeboxes[ idx ];
                head_time  = typebox.getEarliestTime();
                tail_time  = typebox.getLatestTime();
                if ( isPixelIn( coord_xform, pt,
                                head_time, head_ypos,
                                tail_time, tail_ypos ) )
                    return typebox;
            }
            if ( hasBoundary ) {
                head_ypos -= avebox_height;
                tail_ypos += avebox_height;
                head_time  = curr_timebox.getEarliestTime();
                tail_time  = curr_timebox.getLatestTime();
                if ( isPixelIn( coord_xform, pt,
                                head_time, head_ypos,
                                tail_time, tail_ypos ) )
                    return avebox;
            }
        }
        else { // OverlapXXclusionRatio
            if (   head_ypos + avebox_height
                 < tail_ypos - avebox_height ) {
                head_ypos  += avebox_height;
                tail_ypos  -= avebox_height;
                hasBoundary = true;
            }
            else
                hasBoundary = false;
            gap_ypos   = ( tail_ypos - head_ypos ) / ( typeboxes.length * 2 );
            head_ypos += gap_ypos * (typeboxes.length-1);
            tail_ypos -= gap_ypos * (typeboxes.length-1);
            for ( idx = 0; idx < typeboxes.length; idx++ ) {
                typebox    = typeboxes[ idx ];
                head_time  = typebox.getEarliestTime();
                tail_time  = typebox.getLatestTime();
                if ( isPixelIn( coord_xform, pt,
                                head_time, head_ypos,
                                tail_time, tail_ypos ) )
                    return typebox;
                head_ypos -= gap_ypos;
                tail_ypos += gap_ypos;
            }
            if ( hasBoundary ) {
                head_ypos += gap_ypos;
                tail_ypos -= gap_ypos;
                head_ypos -= avebox_height;
                tail_ypos += avebox_height;
                head_time  = curr_timebox.getEarliestTime();
                tail_time  = curr_timebox.getLatestTime();
                if ( isPixelIn( coord_xform, pt,
                                head_time, head_ypos,
                                tail_time, tail_ypos ) )
                    return avebox;
            }
        }

        return null;
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.topology;

import java.awt.Graphics2D;
import java.awt.Stroke;
import java.awt.Color;
import java.awt.Point;
import java.awt.geom.Line2D;
import base.drawable.CoordPixelXform;
import base.drawable.DrawnBox;

public class Arrow
{
    private static       int     Head_Length      = 15;
    private static       int     Head_Half_Width  = 5;

    //  For Viewer 
    public static void setHeadLength( int new_length )
    {
        Head_Length = new_length;
    }

    //  For Viewer 
    public static void setHeadWidth( int new_width )
    {
        Head_Half_Width = new_width / 2;
        if ( Head_Half_Width < 1 )
            Head_Half_Width = 1;
    }

    /*
        Draw an Arrow between 2 vertices
        (start_time, start_ypos) and (final_time, final_ypos)
        Asssume caller guarantees : start_time <= final_time
    */
    private static int  drawForward( Graphics2D g, Color color, Stroke stroke,
                                     CoordPixelXform    coord_xform,
                                     DrawnBox           last_drawn_pos,
                                     double start_time, float start_ypos,
                                     double final_time, float final_ypos )
    {
        int      iStart, jStart, iFinal, jFinal;
        iStart   = coord_xform.convertTimeToPixel( start_time );
        iFinal   = coord_xform.convertTimeToPixel( final_time );

        /* Determine if Arrow should be drawn */
        if ( last_drawn_pos.coversArrow( iStart, iFinal ) )
            return 0; // Arrow has been drawn at the same location before
        last_drawn_pos.set( iStart, iFinal );

        jStart   = coord_xform.convertRowToPixel( start_ypos );
        jFinal   = coord_xform.convertRowToPixel( final_ypos );

        boolean  isStartVtxInImg, isFinalVtxInImg;
        isStartVtxInImg = iStart > 0 ;
        isFinalVtxInImg = iFinal < coord_xform.getImageWidth();

        boolean  isSlopeNonComputable = false;
        double   slope = 0.0;
        if ( iStart != iFinal )
            slope = (double) ( jFinal - jStart ) / ( iFinal - iStart );
        else {
            isSlopeNonComputable = true;
            if ( jStart != jFinal )
                if ( jFinal > jStart )
                    slope = Double.POSITIVE_INFINITY;
                else
                    slope = Double.NEGATIVE_INFINITY;
            else
                // iStart==iFinal, jStart==jFinal, same point
                slope = Double.NaN;
        }

        int iHead, iTail, jHead, jTail;

        /* The main line */
        // jHead = slope * ( iHead - iStart ) + jStart
        if ( isStartVtxInImg ) {
            iHead = iStart;
            jHead = jStart;
        }
        else {
            if ( isSlopeNonComputable )
                return 0; // Arrow NOT in image
            iHead = 0;
            jHead = (int) Math.rint( jStart - slope * iStart );
        }

        // jTail = slope * ( iTail - iFinal ) + jFinal
        if ( isFinalVtxInImg ) {
            iTail = iFinal;
            jTail = jFinal;
        }
        else {
            if ( isSlopeNonComputable )
                return 0; // Arrow NOT in image
            iTail = coord_xform.getImageWidth();
            jTail = (int) Math.rint( jFinal + slope * ( iTail - iFinal ) );
        }

        int iLeft, jLeft, iRight, jRight;

        iLeft = 0; jLeft = 0; iRight = 0; jRight = 0;
        if ( isFinalVtxInImg ) {
            /* The left line */
            double cosA, sinA;
            double xBase, yBase, xOff, yOff;
            if ( isSlopeNonComputable ) {
                if ( slope == Double.NaN ) {
                    cosA =  1.0d;
                    sinA =  0.0d;
                }
                else {
                    if ( slope == Double.POSITIVE_INFINITY ) {
                        cosA =  0.0d;
                        sinA =  1.0d;
                    }
                    else {
                        cosA =  0.0d;
                        sinA = -1.0d;
                    }
                }
            }
            else {
                cosA = 1.0d / Math.sqrt( 1.0d + slope * slope );
                sinA = slope * cosA;
            }
            xBase  = iTail - Head_Length * cosA;
            yBase  = jTail - Head_Length * sinA;
            xOff   = Head_Half_Width * sinA;
            yOff   = Head_Half_Width * cosA;
            iLeft  = (int) Math.round( xBase + xOff );
            jLeft  = (int) Math.round( yBase - yOff );
            iRight = (int) Math.round( xBase - xOff );
            jRight = (int) Math.round( yBase + yOff );
        }

        Stroke orig_stroke = null;
        if ( stroke != null ) {
            orig_stroke = g.getStroke();
            g.setStroke( stroke );
        }

        g.setColor( color );
        // Draw the main line with possible characteristic from stroke
        g.drawLine( iHead, jHead, iTail, jTail );

        if ( stroke != null )
            g.setStroke( orig_stroke );

        // Draw the arrow head without stroke's effect
        if ( isFinalVtxInImg ) {
            g.drawLine( iTail,  jTail,   iLeft,  jLeft );
            g.drawLine( iLeft,  jLeft,   iRight, jRight );
            g.drawLine( iRight, jRight,  iTail,  jTail );
        }

        return 1;
    }

    /*
        Draw an Arrow between 2 vertices
        (start_time, start_ypos) and (final_time, final_ypos)
        Asssume caller guarantees : final_time <= start_time
    */
    private static int  drawBackward( Graphics2D g, Color color, Stroke stroke,
                                      CoordPixelXform    coord_xform,
                                      DrawnBox           last_drawn_pos,
                                      double start_time, float start_ypos,
                                      double final_time, float final_ypos )
    {
        int      iStart, jStart, iFinal, jFinal;
        iStart   = coord_xform.convertTimeToPixel( start_time );
        iFinal   = coord_xform.convertTimeToPixel( final_time );

        /* Determine if Arrow should be drawn */
        if ( last_drawn_pos.coversArrow( iStart, iFinal ) )
            return 0; // Arrow has been drawn at the same location before
        last_drawn_pos.set( iStart, iFinal );

        jStart   = coord_xform.convertRowToPixel( start_ypos );
        jFinal   = coord_xform.convertRowToPixel( final_ypos );

        boolean  isStartVtxInImg, isFinalVtxInImg;
        isStartVtxInImg = iStart < coord_xform.getImageWidth();
        isFinalVtxInImg = iFinal > 0;

        boolean  isSlopeNonComputable = false;
        double   slope = 0.0;
        if ( iStart != iFinal )
            slope = (double) ( jStart - jFinal ) / ( iStart - iFinal );
        else {
            isSlopeNonComputable = true;
            if ( jStart != jFinal )
                if ( jStart > jFinal )
                    slope = Double.POSITIVE_INFINITY;
                else
                    slope = Double.NEGATIVE_INFINITY;
            else
                // iStart==iFinal, jStart==jFinal, same point
                slope = Double.NaN;
        }

        int iHead, iTail, jHead, jTail;

        /* The main line */
        // jHead = slope * ( iHead - iStart ) + jStart
        if ( isStartVtxInImg ) {
            iHead = iStart;
            jHead = jStart;
        }
        else {
            if ( isSlopeNonComputable )
                return 0; // Arrow NOT in image
            iHead = coord_xform.getImageWidth();
            jHead = (int) Math.rint( jStart + slope * ( iHead - iStart ) );
        }

        // jTail = slope * ( iTail - iFinal ) + jFinal
        if ( isFinalVtxInImg ) {
            iTail = iFinal;
            jTail = jFinal;
        }
        else {
            if ( isSlopeNonComputable )
                return 0; // Arrow NOT in image
            iTail = 0;
            jTail = (int) Math.rint( jFinal - slope * iFinal );
        }

        int iLeft, jLeft, iRight, jRight;

        iLeft = 0; jLeft = 0; iRight = 0; jRight = 0;
        if ( isFinalVtxInImg ) {
            /* The left line */
            double cosA, sinA;
            double xBase, yBase, xOff, yOff;
            if ( isSlopeNonComputable ) {
                if ( slope == Double.NaN ) {
                    cosA = -1.0d;
                    sinA =  0.0d;
                }
                else {
                    if ( slope == Double.POSITIVE_INFINITY ) {
                        cosA =  0.0d;
                        sinA = -1.0d;
                    }
                    else {
                        cosA =  0.0d;
                        sinA =  1.0d;
                    }
                }
            }
            else {
                cosA = - 1.0d / Math.sqrt( 1.0d + slope * slope );
                sinA = slope * cosA;
            }
            xBase  = iTail - Head_Length * cosA;
            yBase  = jTail - Head_Length * sinA;
            xOff   = Head_Half_Width * sinA;
            yOff   = Head_Half_Width * cosA;
            iLeft  = (int) Math.round( xBase + xOff );
            jLeft  = (int) Math.round( yBase - yOff );
            iRight = (int) Math.round( xBase - xOff );
            jRight = (int) Math.round( yBase + yOff );
        }

        Stroke orig_stroke = null;
        if ( stroke != null ) {
            orig_stroke = g.getStroke();
            g.setStroke( stroke );
        }
        g.setColor( color );

        // Draw the main line
        g.drawLine( iTail, jTail, iHead, jHead );
        // Draw the arrow head
        if ( isFinalVtxInImg ) {
            g.drawLine( iTail,  jTail,   iLeft,  jLeft );
            g.drawLine( iLeft,  jLeft,   iRight, jRight );
            g.drawLine( iRight, jRight,  iTail,  jTail );
        }

        if ( stroke != null )
            g.setStroke( orig_stroke );

        return 1;
    }


    public static int  draw( Graphics2D g, Color color, Stroke stroke,
                             CoordPixelXform    coord_xform,
                             DrawnBox           last_drawn_pos,
                             double start_time, float start_ypos,
                             double final_time, float final_ypos )
    {
        if ( start_time < final_time )
            return drawForward( g, color, stroke,
                                coord_xform, last_drawn_pos,
                                start_time, start_ypos,
                                final_time, final_ypos );
        else
            return drawBackward( g, color, stroke,
                                 coord_xform, last_drawn_pos,
                                 start_time, start_ypos,
                                 final_time, final_ypos ); 
    }
}

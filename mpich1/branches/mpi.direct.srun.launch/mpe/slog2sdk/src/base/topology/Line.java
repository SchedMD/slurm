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

public class Line
{
    private static double  Max_LineSeg2Pt_DistSQ  = 10.0d;

    public static void setPixelClosenessTolerance( int pix_dist )
    {
        // add 1 at the end so containsPixel() can use "<" instead of "<="
        Max_LineSeg2Pt_DistSQ = (double) ( pix_dist * pix_dist + 1 );
    }
    /*
        Draw a Line between 2 vertices
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
            return 0;  // Line has been drawn at the same location before
        last_drawn_pos.set( iStart, iFinal );

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

    /*
        Check if a point in pixel coordinate is on a Line
        specified between left-upper vertex (start_time, start_ypos) 
        and right-lower vertex (final_time, final_ypos)
    */
    public static boolean containsPixel( CoordPixelXform coord_xform, Point pt,
                                         double start_time, float start_ypos,
                                         double final_time, float final_ypos )
    {
        double   xStart, yStart, xFinal, yFinal;
        double   xPt, yPt;
        double   distSQ;

        xStart   = (double) coord_xform.convertTimeToPixel( start_time );
        yStart   = (double) coord_xform.convertRowToPixel( start_ypos );

        xFinal   = (double) coord_xform.convertTimeToPixel( final_time );
        yFinal   = (double) coord_xform.convertRowToPixel( final_ypos );

        xPt      = (double) pt.x;
        yPt      = (double) pt.y;

        distSQ   = Line2D.ptSegDistSq( xStart, yStart, xFinal, yFinal,
                                       xPt, yPt );
        return distSQ < Max_LineSeg2Pt_DistSQ;
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
            return drawForward( g, color, stroke,
                                coord_xform, last_drawn_pos,
                                final_time, final_ypos,
                                start_time, start_ypos );
    }
}

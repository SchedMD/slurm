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

public class PreviewEvent
{
    private static       double  Max_LineSeg2Pt_DistSQ  = 10.0d;

    public static void setPixelClosenessTolerance( int pix_dist )
    {
        // add 1 at the end so containsPixel() can use "<" instead of "<="
        Max_LineSeg2Pt_DistSQ = (double) ( pix_dist * pix_dist + 1 );
    }

    public static int  draw( Graphics2D g, Color color, Stroke stroke,
                             CoordPixelXform    coord_xform,
                             DrawnBox           last_drawn_pos,
                             double start_time, float start_ypos,
                             double final_time, float final_ypos,
                             double point_time, float point_ypos )
    {
        int      iPoint;
        iPoint   = coord_xform.convertTimeToPixel( point_time );

        /* Determine if Event should be drawn */
        if ( last_drawn_pos.coversEvent( iPoint ) )
            return 0; // Event has been drawn at the same location before
        last_drawn_pos.set( iPoint );

        boolean  isPointVtxInImg;
        isPointVtxInImg = iPoint > 0 && iPoint < coord_xform.getImageWidth();

        if ( ! isPointVtxInImg )
            return 0;

        int      iStart, iFinal, jPoint, jStart, jFinal;
        iStart   = coord_xform.convertTimeToPixel( start_time );
        iFinal   = coord_xform.convertTimeToPixel( final_time );
        jPoint   = coord_xform.convertRowToPixel( point_ypos );
        jStart   = coord_xform.convertRowToPixel( start_ypos );
        jFinal   = coord_xform.convertRowToPixel( final_ypos );

        Stroke orig_stroke = null;
        if ( stroke != null ) {
            orig_stroke = g.getStroke();
            g.setStroke( stroke );
        }

        int  iCornerLeft, iCornerRight, iWidthLeft, iWidthRight, jHeight;
        iCornerLeft  = 0;
        iCornerRight = 0;
        iWidthLeft   = 0;
        iWidthRight  = 0;
        jHeight      = 0;

        g.setColor( color );
        /* Fill the asymetrical ellipse first */
        if ( jStart != jPoint ) {
            jHeight = jPoint - jStart - 1;
            iCornerLeft = iStart;
            iWidthLeft  = 2 * (iPoint - iStart);
            g.fillArc( iCornerLeft, jStart, iWidthLeft, jHeight, 90, 180 );
            iCornerRight = 2 * iPoint - iFinal;
            iWidthRight  = 2 * (iFinal - iPoint);
            g.fillArc( iCornerRight, jStart, iWidthRight, jHeight, 90, -180 );
        }

        g.setColor( Color.white );
        g.drawLine( iPoint, jPoint, iPoint, jFinal );
        /* Draw the white asymetrical ellipse boundary */
        if ( jStart != jPoint ) {
            g.drawArc( iCornerLeft, jStart, iWidthLeft, jHeight, 90, 180 );
            g.drawArc( iCornerRight, jStart, iWidthRight, jHeight, 90, -180 );
        }

        if ( stroke != null )
            g.setStroke( orig_stroke );

        return 1;
    }

    public static boolean  containsPixel( CoordPixelXform coord_xform, Point pt,
                                          double start_time, float start_ypos,
                                          double final_time, float final_ypos,
                                          double point_time, float point_ypos )
    {
        int      iPoint, iStart, iFinal, jPoint, jStart, jFinal;
        int      pt_x, pt_y;
        double   distSQ;

        pt_x     = pt.x;
        pt_y     = pt.y;

        // Check if it is within the bounding box of the event
        jStart   = coord_xform.convertRowToPixel( start_ypos );
        if ( pt_y < jStart )
            return false;

        jFinal   = coord_xform.convertRowToPixel( final_ypos );
        if ( pt_y > jFinal )
            return false;

        iStart   = coord_xform.convertTimeToPixel( start_time );
        if ( pt_x < iStart )
            return false;

        iFinal   = coord_xform.convertTimeToPixel( final_time );
        if ( pt_x > iFinal )
            return false;

        // Check if within the vicinity of the vertical line below the "cloud".
        iPoint   = coord_xform.convertTimeToPixel( point_time );
        jPoint   = coord_xform.convertRowToPixel( point_ypos );

        distSQ   = Line2D.ptSegDistSq( (double) iPoint, (double) jPoint,
                                       (double) iPoint, (double) jFinal,
                                       (double) pt_x, (double) pt_y );
        if ( distSQ < Max_LineSeg2Pt_DistSQ )
            return true;

        // Check if the point is within the asymetrical ellipse
        double fCornerLeft, fCornerRight;
        double fHalfWidthLeft, fHalfWidthRight, fHalfHeight;
        if ( jStart != jPoint ) {
            fHalfHeight = ( (double) (jPoint - jStart) ) / 2.0;
            if ( pt_x < iPoint ) {
                fCornerLeft     = (double) iStart;
                fHalfWidthLeft  = (double) (iPoint - iStart);
                if ( isInEllipse( fCornerLeft, (double) jStart,
                                  fHalfWidthLeft, fHalfHeight,
                                  (double) pt_x, (double) pt_y ) )
                    return true;
            }
            else {  /*  if ( pt_x >= iPoint )  */
                fCornerRight = (double) (2 * iPoint - iFinal);
                fHalfWidthRight  = (double) (iFinal - iPoint);
                if ( isInEllipse( fCornerRight, (double) jStart,
                                  fHalfWidthRight, fHalfHeight,
                                  (double) pt_x, (double) pt_y ) )
                    return true;
            }
        }

        return false;
    }

    private static boolean isInEllipse( double xCorner, double yCorner,
                                        double xHalfWidth, double yHalfHeight,
                                        double pt_x, double pt_y )
    {
        /*
            xDist and yDist are scaled distance measured from the center
            of ellipse at (xCorner + xHalfWidth, yCorner + yHalfHeight).

            eqn for ellipse :   xDist^2 + xDist^2 = 1 
        */
        double xDist, yDist;

        xDist = (pt_x - xCorner)/xHalfWidth  - 1.0;
        yDist = (pt_y - yCorner)/yHalfHeight - 1.0;

        return (xDist*xDist + yDist*yDist) <= 1.0;
    }
}

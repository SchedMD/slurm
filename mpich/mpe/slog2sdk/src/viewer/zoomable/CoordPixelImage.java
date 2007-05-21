/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import base.drawable.TimeBoundingBox;
import base.drawable.CoordPixelXform;

public class CoordPixelImage implements CoordPixelXform
{
    private ScrollableObject  img_obj;
    private int               row_hgt;
    private int               row_half_hgt;

    private TimeBoundingBox   img_endtimes;
    private double            img_starttime;
    private double            img_finaltime;
    private int               ipix_start;
    private int               ipix_final;
    private int               ipix_width;

    public CoordPixelImage( ScrollableObject image_object )
    {
        img_obj        = image_object;
        row_hgt        = 0;
        row_half_hgt   = 0;
    }

    public CoordPixelImage( ScrollableObject image_object, int row_height,
                            final TimeBoundingBox  image_timebounds )
    {
        this( image_object );
        this.resetRowHeight( row_height );
        this.resetTimeBounds( image_timebounds );
    }

    public void resetRowHeight( int row_height )
    {
        row_hgt        = row_height;
        row_half_hgt   = row_height / 2 + 1;
    }

    public void resetTimeBounds( final TimeBoundingBox  image_timebounds )
    {
        img_endtimes   = image_timebounds;
        img_starttime  = image_timebounds.getEarliestTime();
        img_finaltime  = image_timebounds.getLatestTime();
        ipix_start     = img_obj.time2pixel( img_starttime );
        ipix_final     = img_obj.time2pixel( img_finaltime );
        ipix_width     = ipix_final - ipix_start + 1;
    }

    public int     convertTimeToPixel( double time_coord )
    {
        return img_obj.time2pixel( time_coord ) - ipix_start;
    }

    public double  convertPixelToTime( int hori_pixel )
    {
        return img_obj.pixel2time( hori_pixel + ipix_start );
    }

    public int     convertRowToPixel( float rowID )
    {
        return Math.round( rowID * row_hgt + row_half_hgt );
    }

    public float   convertPixelToRow( int vert_pixel )
    {
        return  (float) ( vert_pixel - row_half_hgt ) / row_hgt;
    }

    /*
    public double  getBoundedStartTime( double starttime )
    {
        return ( starttime > img_starttime ?  starttime :  img_starttime );
    }

    public double  getBoundedFinalTime( double finaltime )
    {
        return ( finaltime < img_finaltime ?  finaltime :  img_finaltime );
    }
    */

    public boolean contains( double time_coord )
    {
        return img_endtimes.contains( time_coord );
    }

    public boolean overlaps( final TimeBoundingBox  timebox )
    {
        return img_endtimes.overlaps( timebox );
    }

    public int     getImageWidth()
    {
        return ipix_width;
    }
}

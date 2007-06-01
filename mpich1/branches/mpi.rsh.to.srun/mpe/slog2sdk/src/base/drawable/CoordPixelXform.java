/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

public interface CoordPixelXform
{
    public int     convertTimeToPixel( double time_coord );

    public double  convertPixelToTime( int hori_pixel );

    public int     convertRowToPixel( float rowID );

    public float   convertPixelToRow( int vert_pixel );

    // public double  getBoundedStartTime( double starttime );

    // public double  getBoundedFinalTime( double finaltime );

    public boolean contains( double time_coord );

    public boolean overlaps( final TimeBoundingBox  timebox );

    public int     getImageWidth();
}

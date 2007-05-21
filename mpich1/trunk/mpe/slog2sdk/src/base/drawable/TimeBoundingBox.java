/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.util.Comparator;
import java.io.DataOutput;
import java.io.DataInput;

import base.io.DataIO;

public class TimeBoundingBox implements DataIO
{
    public  static final TimeBoundingBox  ALL_TIMES
                         = new TimeBoundingBox( Double.NEGATIVE_INFINITY,
                                                Double.POSITIVE_INFINITY );

    public  static final Order            INCRE_STARTTIME_ORDER
                         = new IncreasingStarttimeOrder();

    public  static final Order            DECRE_STARTTIME_ORDER
                         = new DecreasingStarttimeOrder();

    public  static final Order            INCRE_FINALTIME_ORDER
                         = new IncreasingFinaltimeOrder();

    public  static final Order            DECRE_FINALTIME_ORDER
                         = new DecreasingFinaltimeOrder();

    public  static final int              BYTESIZE = 8  /* earliest_time */
                                                   + 8  /* latest_time */  ;

    private              double           earliest_time;
    private              double           latest_time;

    public TimeBoundingBox()
    {
        earliest_time = Double.POSITIVE_INFINITY;
        latest_time   = Double.NEGATIVE_INFINITY;
    }

    public TimeBoundingBox( final TimeBoundingBox timebox )
    {
        earliest_time = timebox.earliest_time;
        latest_time   = timebox.latest_time;
    }

    public TimeBoundingBox( final Coord[] vtxs )
    {
        if ( vtxs != null ) {
            earliest_time  = latest_time  = vtxs[ 0 ].time;
            for ( int idx = 1; idx < vtxs.length; idx++ ) {
                this.affectEarliestTime( vtxs[ idx ].time );
                this.affectLatestTime( vtxs[ idx ].time );
            }
        }
        else {
            earliest_time = Double.POSITIVE_INFINITY;
            latest_time   = Double.NEGATIVE_INFINITY;
        }
    }

    private TimeBoundingBox( double starttime, double finaltime )
    {
        earliest_time = starttime;
        latest_time   = finaltime;
    }

    public void reinitialize()
    {
        earliest_time = Double.POSITIVE_INFINITY;
        latest_time   = Double.NEGATIVE_INFINITY;
    }

    public void affectTimeBounds( final TimeBoundingBox endtimes )
    {
        this.affectEarliestTime( endtimes.getEarliestTime() );
        this.affectLatestTime( endtimes.getLatestTime() );
    }

    public void affectTimeBounds( final Coord vtx )
    {
        this.affectEarliestTime( vtx.time );
        this.affectLatestTime( vtx.time );
    }

    public void affectTimeBounds( final Coord[] vtxs )
    {
        for ( int idx = 0; idx < vtxs.length; idx++ ) {
            this.affectEarliestTime( vtxs[ idx ].time );
            this.affectLatestTime( vtxs[ idx ].time );
        }
    }


    public void affectEarliestTime( double in_time )
    {
        if ( in_time < earliest_time )
            earliest_time = in_time;
    }

    public void setEarliestTime( double in_time )
    {
        earliest_time = in_time;
    }

    public double getEarliestTime()
    {
        return earliest_time;
    }


    public void affectLatestTime( double in_time )
    {
        if ( in_time > latest_time )
            latest_time = in_time;
    }

    public void setLatestTime( double in_time )
    {
        latest_time = in_time;
    }

    public double getLatestTime()
    {
        return latest_time;
    }


    public double getBorderTime( boolean isStartTime )
    {
        if ( isStartTime )
            return earliest_time;
        else
            return latest_time;
    }


    // Functions useful in ScrollableObject
    // This is used after this.setEarliestTime() is invoked.
    // time_extent is positive definite
    public void setLatestFromEarliest( double time_extent )
    {
        latest_time = earliest_time + time_extent;
    }

    // This is used after this.setLatestTime() is invoked.
    // time_extent is positive definite
    public void setEarliestFromLatest( double time_extent )
    {
        earliest_time = latest_time - time_extent;
    }

    public boolean isTimeOrdered()
    {
        return earliest_time <= latest_time;
    }

    // Return true when endtimes covers the one end of this.TimeBoundingBox
    public boolean remove( final TimeBoundingBox  endtimes )
    {
        if ( this.earliest_time == endtimes.earliest_time ) {
            this.earliest_time = endtimes.latest_time; 
            return true;
        }
        if ( this.latest_time == endtimes.latest_time ) {
            this.latest_time = endtimes.earliest_time; 
            return true;
        }
        return false;
    }

    //  TimeBoundingBox Checking Routines for SLOG-2 Input API
    /*
       Logic concerning overlaps(), covers() and disjoints()
       1) covers()      implies  overlaps().
       2) !overlaps()   implies  disjoints().
       3) !disjoints()  implies  overlaps().
    */
    public boolean covers( final TimeBoundingBox endtimes )
    {
        return    ( this.earliest_time <= endtimes.earliest_time )
               && ( endtimes.latest_time <= this.latest_time );
    }

    public boolean overlaps( final TimeBoundingBox endtimes )
    {
        return    ( this.earliest_time <= endtimes.latest_time )
               && ( endtimes.earliest_time <= this.latest_time );
    }

    // For consistence: Avoid using disjoints(), use !overlaps() instead
    public boolean disjoints( final TimeBoundingBox endtimes )
    {
        return    ( this.latest_time < endtimes.earliest_time )
               || ( endtimes.latest_time < this.earliest_time );
    }

    public boolean contains( double timestamp )
    {
        return    ( this.earliest_time <= timestamp )
               && ( timestamp <= this.latest_time );
    }

    public boolean equals( final TimeBoundingBox endtimes )
    {
        return    ( this.earliest_time == endtimes.earliest_time )
               && ( this.latest_time == endtimes.latest_time );
    }

    /*
       containsWithinLeft()/containsWithinRight() are for logformat.slog2.Print
       Or they are for logformat.slog2.input.InputLog.iterator()
    */
    public boolean containsWithinLeft( double timestamp )
    {
        return    ( this.earliest_time <= timestamp )
               && ( timestamp < this.latest_time );
    }

    public boolean containsWithinRight( double timestamp )
    {
        return    ( this.earliest_time < timestamp )
               && ( timestamp <= this.latest_time );
    }

    public TimeBoundingBox getIntersection( final TimeBoundingBox endtimes )
    {
        TimeBoundingBox  intersect_endtimes;
        double           intersect_earliest_time, intersect_latest_time;

        if ( this.overlaps( endtimes ) ) {
            if ( this.earliest_time < endtimes.earliest_time )
                intersect_earliest_time = endtimes.earliest_time;
            else
                intersect_earliest_time = this.earliest_time;
            if ( this.latest_time < endtimes.latest_time )
                intersect_latest_time   = this.latest_time;
            else
                intersect_latest_time   = endtimes.latest_time;
            intersect_endtimes = new TimeBoundingBox();
            intersect_endtimes.earliest_time  = intersect_earliest_time;
            intersect_endtimes.latest_time    = intersect_latest_time;
            return intersect_endtimes;
        }
        else
            return null;
    }

    public double getIntersectionDuration( final TimeBoundingBox endtimes )
    {
        double           intersect_earliest_time, intersect_latest_time;
        double           intersect_duration;

        if ( this.overlaps( endtimes ) ) {
            if ( this.earliest_time < endtimes.earliest_time )
                intersect_earliest_time = endtimes.earliest_time;
            else
                intersect_earliest_time = this.earliest_time;
            if ( this.latest_time < endtimes.latest_time )
                intersect_latest_time   = this.latest_time;
            else
                intersect_latest_time   = endtimes.latest_time;
            intersect_duration  = intersect_latest_time
                                - intersect_earliest_time;
            if ( intersect_duration > 0.0d )
                return intersect_duration;
            else
                return 0.0d;
        }
        else
            return 0.0d;
    }

    /* For SLOG-2 Input API & viewer */
    public double getDuration()
    {
        return latest_time - earliest_time;
    }

    public void setZeroDuration( double time )
    {
        earliest_time = time;
        latest_time   = time;
    }


    public static void writeObject( final TimeBoundingBox  timebox,
                                          DataOutput       outs )
    throws java.io.IOException
    {
        outs.writeDouble( timebox.earliest_time );
        outs.writeDouble( timebox.latest_time );
        // timebox.writeObject( outs ) invokes InfoBox.writeObject( outs )
    }

    public static void readObject( TimeBoundingBox  timebox,
                                   DataInput        ins )
    throws java.io.IOException
    {
        timebox.earliest_time  = ins.readDouble();
        timebox.latest_time    = ins.readDouble();
    }
  
    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeDouble( earliest_time );
        outs.writeDouble( latest_time );
    }

    public TimeBoundingBox( DataInput ins )
    throws java.io.IOException
    {
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        earliest_time  = ins.readDouble();
        latest_time    = ins.readDouble();
    }

    public String toString()
    {
        /*
        if ( latest_time - earliest_time >= 0 )
            return ( "TimeBBox( + )" );
        else
            return ( "TimeBBox( - )" );
        */
        return ( "TimeBBox(" + earliest_time + "," + latest_time + ")" );
        /*
        return ( "TimeBBox(" + (float) earliest_time
                       + "," + (float) latest_time + ")" );
        */
    }



    /*
        Define TimeBoundingBox.Order as an alias of java.util.Comparator
    */
    public interface Order extends Comparator
    {
        public boolean isIncreasingTimeOrdered();
        public boolean isStartTimeOrdered();
        public String  toString();
    }

/*
    This comparator to Collections.sort() will arrange TimeBoundingBoxs
    in increasing starttime order.  If starttimes are equals, TimeBoundingBox
    will then be arranged in decreasing finaltime order.
*/
    private static class IncreasingStarttimeOrder implements Order
    {
        public int compare( Object o1, Object o2 )
        {
            TimeBoundingBox  timebox1, timebox2;
            timebox1 = (TimeBoundingBox) o1;
            timebox2 = (TimeBoundingBox) o2;
            if ( timebox1.earliest_time != timebox2.earliest_time )
                // increasing starttime order ( 1st order )
                return ( timebox1.earliest_time < timebox2.earliest_time
                       ? -1 : 1 );
            else {
                if ( timebox1.latest_time != timebox2.latest_time )
                    // decreasing finaltime order ( 2nd order )
                    return ( timebox1.latest_time > timebox2.latest_time
                           ? -1 : 1 );
                else {
                    // if ( timebox1 == timebox2 )
                        return 0;
                }   // FinalTime
            }   // StartTime
        }

        public boolean isIncreasingTimeOrdered() {return true;}
        public boolean isStartTimeOrdered() {return true;}
        public String toString() {return "INCRE_STARTTIME_ORDER";}
    }

/*
    This comparator to Collections.sort() will arrange TimeBoundingBoxs
    in decreasing starttime order.  If starttimes are equals, TimeBoundingBox
    will then be arranged in increasing finaltime order.
*/
    private static class DecreasingStarttimeOrder implements Order
    {
        public int compare( Object o1, Object o2 )
        {
            TimeBoundingBox  timebox1, timebox2;
            timebox1 = (TimeBoundingBox) o1;
            timebox2 = (TimeBoundingBox) o2;
            if ( timebox1.earliest_time != timebox2.earliest_time )
                // decreasing starttime order ( 1st order )
                return ( timebox1.earliest_time > timebox2.earliest_time
                       ? -1 : 1 );
            else {
                if ( timebox1.latest_time != timebox2.latest_time )
                    // increasing finaltime order ( 2nd order )
                    return ( timebox1.latest_time < timebox2.latest_time
                           ? -1 : 1 );
                else {
                    // if ( timebox1 == timebox2 )
                        return 0;
                }   // FinalTime
            }   // StartTime
        }

        public boolean isIncreasingTimeOrdered() {return false;}
        public boolean isStartTimeOrdered() {return true;}
        public String toString() {return "DECRE_STARTTIME_ORDER";}
    }

/*
    This comparator to Collections.sort() will arrange TimeBoundingBoxs
    in increasing finaltime order.  If finaltimes are equals, TimeBoundingBox
    will then be arranged in decreasing starttime order.
*/
    private static class IncreasingFinaltimeOrder implements Order
    {
        public int compare( Object o1, Object o2 )
        {
            TimeBoundingBox  timebox1, timebox2;
            timebox1 = (TimeBoundingBox) o1;
            timebox2 = (TimeBoundingBox) o2;
            if ( timebox1.latest_time != timebox2.latest_time )
                // increasing finaltime order ( 1st order )
                return ( timebox1.latest_time < timebox2.latest_time
                       ? -1 : 1 );
            else {
                if ( timebox1.earliest_time != timebox2.earliest_time )
                    // decreasing starttime order ( 2nd order )
                    return ( timebox1.earliest_time > timebox2.earliest_time
                           ? -1 : 1 );
                else {
                    // if ( timebox1 == timebox2 )
                        return 0;
                }   // StartTime
            }   // FinalTime
        }

        public boolean isIncreasingTimeOrdered() {return true;}
        public boolean isStartTimeOrdered() {return false;}
        public String toString() {return "INCRE_FINALTIME_ORDER";}
    }

/*
    This comparator to Collections.sort() will arrange TimeBoundingBoxs
    in decreasing finaltime order.  If finaltimes are equals, TimeBoundingBox
    will then be arranged in increasing starttime order.
*/
    private static class DecreasingFinaltimeOrder implements Order
    {
        public int compare( Object o1, Object o2 )
        {
            TimeBoundingBox  timebox1, timebox2;
            timebox1 = (TimeBoundingBox) o1;
            timebox2 = (TimeBoundingBox) o2;
            if ( timebox1.latest_time != timebox2.latest_time )
                // increasing finaltime order ( 1st order )
                return ( timebox1.latest_time > timebox2.latest_time
                       ? -1 : 1 );
            else {
                if ( timebox1.earliest_time != timebox2.earliest_time )
                    // decreasing starttime order ( 2nd order )
                    return ( timebox1.earliest_time < timebox2.earliest_time
                           ? -1 : 1 );
                else {
                    // if ( timebox1 == timebox2 )
                        return 0;
                }   // StartTime
            }   // FinalTime
        }

        public boolean isIncreasingTimeOrdered() {return false;}
        public boolean isStartTimeOrdered() {return false;}
        public String toString() {return "DECRE_FINALTIME_ORDER";}
    }

    public static final void main( String[] args )
    {
        TimeBoundingBox timebox = new TimeBoundingBox();
        System.out.println( timebox );

        if ( INCRE_STARTTIME_ORDER.equals( DECRE_STARTTIME_ORDER ) )
            System.out.println( "INCRE_STARTTIME_ORDER=DECRE_STARTTIME_ORDER" );
        else
            System.out.println( "INCRE_STARTTIME_ORDER!DECRE_STARTTIME_ORDER" );

        if ( INCRE_STARTTIME_ORDER.equals( INCRE_FINALTIME_ORDER ) )
            System.out.println( "INCRE_STARTTIME_ORDER=INCRE_FINALTIME_ORDER" );
        else
            System.out.println( "INCRE_STARTTIME_ORDER!INCRE_FINALTIME_ORDER" );

        if ( INCRE_STARTTIME_ORDER.equals( DECRE_FINALTIME_ORDER ) )
            System.out.println( "INCRE_STARTTIME_ORDER=DECRE_FINALTIME_ORDER" );
        else
            System.out.println( "INCRE_STARTTIME_ORDER!DECRE_FINALTIME_ORDER" );

        if ( INCRE_STARTTIME_ORDER.equals( INCRE_STARTTIME_ORDER ) )
            System.out.println( "INCRE_STARTTIME_ORDER=INCRE_STARTTIME_ORDER" );
        else
            System.out.println( "INCRE_STARTTIME_ORDER!INCRE_STARTTIME_ORDER" );

        TimeBoundingBox.Order tmp_order = new IncreasingStarttimeOrder();
        if ( INCRE_STARTTIME_ORDER.equals( tmp_order ) )
            System.out.println( "INCRE_STARTTIME_ORDER=tmp_order" );
        else
            System.out.println( "INCRE_STARTTIME_ORDER!tmp_order" );
    }

}

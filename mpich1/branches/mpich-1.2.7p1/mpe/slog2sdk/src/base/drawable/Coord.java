/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.io.DataInput;
import java.io.DataOutput;
import java.util.Comparator;

import base.io.DataIO;

public class Coord implements DataIO
{
    public static final Comparator   LINEID_ORDER = new LineIDOrder();

    public static final int  BYTESIZE = 8  /* time */
                                      + 4  /* lineID */ ;

    public double time;      // time
    public int    lineID;    // y axis ID for the Y axis, i.e. timeline ID

    public Coord( double in_time, int in_task )
    {
        time   = in_time;
        lineID = in_task;
    }

    public Coord( final Coord in_vertex )
    {
        time   = in_vertex.time;
        lineID = in_vertex.lineID;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeDouble( time );
        outs.writeInt( lineID );
    }

    public Coord( DataInput ins )
    throws java.io.IOException
    {
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        time    = ins.readDouble();
        lineID  = ins.readInt();
    }

    public int getByteSize()
    {
        return BYTESIZE;
    }

    public String toString()
    {
        return ( "(" + (float) time + ", " + lineID + ")" );
    }



    private static class LineIDOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            Coord coord1  = (Coord) o1;
            Coord coord2  = (Coord) o2;
            return coord1.lineID - coord2.lineID;
        }
    }
}

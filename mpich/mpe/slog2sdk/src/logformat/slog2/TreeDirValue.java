/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.io.DataInput;
import java.io.DataOutput;

import base.io.DataIO;
import base.drawable.TimeBoundingBox;

public class TreeDirValue implements DataIO
{
    public static final int  BYTESIZE = TimeBoundingBox.BYTESIZE
                                      + FileBlockPtr.BYTESIZE;

    private TimeBoundingBox  timebounds;
    private FileBlockPtr     blockptr;

    public TreeDirValue( final TimeBoundingBox  timebox,
                         final FileBlockPtr     fblkptr )
    {
        timebounds = timebox;
        blockptr   = fblkptr;
    }

    public double getEarliestTime()
    {
        return timebounds.getEarliestTime();
    }

    public double getLatestTime()
    {
        return timebounds.getLatestTime();
    }

    public TimeBoundingBox getTimeBoundingBox()
    {
        return timebounds;
    }

    public long getFilePointer()
    {
        return blockptr.getFilePointer();
    }

    public int  getBlockSize()
    {
        return blockptr.getBlockSize();
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        timebounds.writeObject( outs );
        blockptr.writeObject( outs );
    }

    public TreeDirValue( DataInput ins )
    throws java.io.IOException
    {
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        timebounds = new TimeBoundingBox( ins );
        blockptr   = new FileBlockPtr( ins );
    }

    public String toString()
    {
        return ( "DirVal[ " + timebounds + ", " + blockptr + " ]" );
    }
}

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

public class FileBlockPtr
{
    public static final int BYTESIZE = 8  /* fptr */
                                     + 4  /* size */  ;

    private long fptr;
    private int  size;

    public FileBlockPtr()
    {
        // ( Const.NULL_fptr, 0 ) is assumed to be NULL for FileBlockPtr
        fptr = Const.NULL_fptr;
        size = 0;
    }

    public FileBlockPtr( long in_fptr, int in_size )
    {
        fptr = in_fptr;
        size = in_size;
    }

    public boolean isNULL()
    {
        return ( fptr == Const.NULL_fptr || size <= 0 );
    }

    public void setFileBlockPtr( long in_fptr, int in_size )
    {
        fptr = in_fptr;
        size = in_size;
    }

    public void setFilePointer( long in_fptr )
    {
        fptr = in_fptr;
    }

    public long getFilePointer()
    {
        return fptr;
    }

    public void setBlockSize( int in_size )
    {
        size = in_size;
    }

    public int  getBlockSize()
    {
        return size;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeLong( fptr );
        outs.writeInt( size );
    }

    public FileBlockPtr( DataInput ins )
    throws java.io.IOException
    {
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        fptr = ins.readLong();
        size = ins.readInt();
    }

    public String toString()
    {
        return ( "FBinfo(" + size + " @ " + fptr + ")" );
    }
}

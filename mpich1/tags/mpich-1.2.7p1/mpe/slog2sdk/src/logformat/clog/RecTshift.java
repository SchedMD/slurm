/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;


// Class corresponds to CLOG_TSHIFT
public class RecTshift
{
    public  static final int RECTYPE  = Const.RecType.SHIFT;
    private static final int BYTESIZE = 8;
    public  double timeshift;              // time shift for this process 
  
    public int readFromDataStream( DataInputStream in )
    {
        try {
            timeshift = in.readDouble();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        return BYTESIZE;
    }

    public int skipBytesFromDataStream( DataInputStream in )
    {
        try {
            in.skipBytes( BYTESIZE );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        return BYTESIZE;
    }

    public String toString()
    {
        return ( "RecTshift"
               + "[ timeshift=" + timeshift
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;


// Class corresponds to CLOG_MSG
public class RecMsg 
{
    public  static final int RECTYPE  = Const.RecType.MSGEVENT;    
    private static final int BYTESIZE = 6 * 4;
    public  Integer    etype;       // kind of message event 
    public  int        tag;         // message tag 
    public  int        partner;     // source or destination in send/recv
    public  int        comm;        // communicator
    public  int        size;        // length in bytes 
    public  int        srcloc;      // id of source location
  
    public int readFromDataStream( DataInputStream in )
    {
        try {
            etype    = new Integer( in.readInt() );
            tag      = in.readInt();
            partner  = in.readInt();
            comm     = in.readInt();
            size     = in.readInt();
            srcloc   = in.readInt();
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
        return ( "RecMsg"
               + "[ etype=" + etype
               + ", tag=" + tag
               + ", partner=" +  partner
               + ", comm=" + comm
               + ", size=" + size
               + ", srcloc=" + srcloc
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }
}

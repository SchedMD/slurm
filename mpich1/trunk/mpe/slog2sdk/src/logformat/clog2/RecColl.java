/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;


// Class corresponds to CLOG_Rec_CollEvt
public class RecColl
{
    public  static final int RECTYPE  = Const.RecType.COLLEVT;
    private static final int BYTESIZE = 4 * 4;
    public         Integer  etype;       // type of collective event 
    public         int      root;        // root of collective op 
    public         int      comm;        // communicator
    public         int      size;        // length in bytes 
 
    public int readFromDataStream( DataInputStream in )
    {
        try {
            etype   = new Integer( in.readInt() );
            root    = in.readInt();
            comm    = in.readInt();
            size    = in.readInt();
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
        return ( "RecColl"
               + "[ etype=" + etype
               + ", root=" + root
               + ", comm=" +  comm
               + ", size=" + size
               // + ", BYTESIZE=" + BYTESIZE
               + " ]");
    }
}

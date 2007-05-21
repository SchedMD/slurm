/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;


// Class corresponds to CLOG_COLL
public class RecColl
{
    public  static final int RECTYPE  = Const.RecType.COLLEVENT;
    private static final int BYTESIZE = 6 * 4;
    public         Integer  etype;       // type of collective event 
    public         int      root;        // root of collective op 
    public         int      comm;        // communicator
    public         int      size;        // length in bytes 
    public         int      srcloc;      // id of source location 
    private static int      pad;
 
    public int readFromDataStream( DataInputStream in )
    {
        try {
            etype   = new Integer( in.readInt() );
            root    = in.readInt();
            comm    = in.readInt();
            size    = in.readInt();
            srcloc  = in.readInt();
            pad     = in.readInt();
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
               + ", srcloc=" + srcloc
               // + ", pad=" + pad
               // + ", BYTESIZE=" + BYTESIZE
               + " ]");
    }
}

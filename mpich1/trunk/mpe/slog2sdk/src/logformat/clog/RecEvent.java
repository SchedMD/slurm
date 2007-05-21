/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;


// Class corresponds to CLOG_EVENT
public class RecEvent
{
    public  static final int RECTYPE  = Const.RecType.EVENTDEF;
    private static final int BYTESIZE = 2 * 4
                                      + StrDesc.BYTESIZE;
    public         Integer     etype;            // event
    private static int         pad;              // pad 
    public         String      description;      // string describing event
  
    public int readFromDataStream( MixedDataInputStream in )
    {
        try {
            etype        = new Integer( in.readInt() );
            pad          = in.readInt();
            description  = in.readString( StrDesc.BYTESIZE );
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
        return ( "RecEvent"
               + "[ etype=" + etype
               // + ", pad=" + pad
               + ", desc=" + description
               // + ", BYTESIZE=" + BYTESIZE
               + " ]");
    }
}

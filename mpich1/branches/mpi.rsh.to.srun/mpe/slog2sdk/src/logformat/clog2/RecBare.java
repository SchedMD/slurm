/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;
import java.util.*;


// Class corresponds to CLOG_Rec_BareEvt
public class RecBare
{
    public  static final int RECTYPE  = Const.RecType.BAREEVT;
    private static final int BYTESIZE = 4 + 4;

    public         Integer etype;                  // bare event number
    private static int     pad;

    //read the record from the given input stream
    public int readFromDataStream( MixedDataInputStream in )
    {
        try {
            etype   = new Integer( in.readInt() );
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
        return ( "RecBare"
               + "[ etype=" + etype
               // + ", pad=" + pad 
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }
}

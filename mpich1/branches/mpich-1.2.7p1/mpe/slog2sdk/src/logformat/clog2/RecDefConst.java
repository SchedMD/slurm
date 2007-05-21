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


// Class corresponds to CLOG_Rec_ConstDef
public class RecDefConst
{
    public  static final int RECTYPE  = Const.RecType.CONSTDEF;
    private static final int BYTESIZE = 2 * 4
                                      + StrDesc.BYTESIZE;
    public         Integer etype;                      // constant event type
    public         int     value;                      // constant's value
    public         String  name;                       // constant's name

 
    //read the record from the given input stream
    public int readFromDataStream( MixedDataInputStream in )
    {
        try {
            etype   = new Integer( in.readInt() );
            value   = in.readInt();
            name    = in.readString( StrDesc.BYTESIZE );
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
        return ( "RecDefConst"
               + "[ etype=" + etype
               + ", value=" + value
               + ", name=" + name
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }
}

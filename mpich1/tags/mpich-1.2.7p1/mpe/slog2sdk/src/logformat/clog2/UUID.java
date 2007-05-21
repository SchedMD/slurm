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


public class UUID
{
    // BYTESIZE is visible within logformat.clog2
    private static final int NAMESIZE = 20;
            static final int BYTESIZE = 4 + 8 + NAMESIZE; 

    public         byte[]  bytes;                  // byte data

    //read the record from the given input stream
    public int readFromDataStream( DataInputStream in )
    {
        try {
            bytes   = new byte[ BYTESIZE ];
            in.readFully( bytes );
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
        int                    irand;
        double                 ftime;
        String                 name;
        ByteArrayInputStream   bary_ins;
        DataInputStream        data_ins;

        irand    = 0;
        ftime    = 0.0;
        bary_ins = new ByteArrayInputStream( bytes, 0, 4+8 );
        data_ins = new DataInputStream( bary_ins );
        try {
            irand = data_ins.readInt();
            ftime = data_ins.readDouble();
            data_ins.close();
            bary_ins.close();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }

        name = new String( bytes, 4+8, NAMESIZE );

        return String.valueOf( irand ) + "-"
             + String.valueOf( ftime ) + "-"
             + name;
    }
}

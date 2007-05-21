/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;

// Extend java.io.DataInputStream to include a readString() method
public class MixedDataInputStream extends DataInputStream
{
    public MixedDataInputStream( InputStream in )
    {
        super( in );
    }

    // Return a trimed String from reading the Stream with a temporary buffer
    public String readString( int bytesize ) throws IOException
    {
        byte[] bytebuf = new byte[ bytesize ];
        super.readFully( bytebuf );
        return ( new String( bytebuf ) ).trim();
    }
}

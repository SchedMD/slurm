/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;

// Extend java.io.DataInputStream to include a readString() method
public class MixedDataInputStream extends DataInputStream
{
    public MixedDataInputStream( InputStream in )
    {
        super( in );
    }

    // Extract the java string from the C string buffer byte_buf
    public String readString( int bytesize ) throws IOException
    {
        byte[]  byte_buf;
        String  byte_str;
        int     cstr_end_idx;

        byte_buf = new byte[ bytesize ];
        super.readFully( byte_buf );
        byte_str =  new String( byte_buf );
        cstr_end_idx = byte_str.indexOf( '\0' );
        if ( cstr_end_idx > 0 )
            return byte_str.substring( 0, cstr_end_idx ).trim();
        else
            return null;
    }
}

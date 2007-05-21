/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.InputStream;
import java.io.DataInputStream;

public class MixedDataInputStream extends DataInputStream
                                  implements MixedDataInput
{
    public MixedDataInputStream( InputStream ins )
    {
        super( ins );
    }

    public String readString()
    throws java.io.IOException
    {
        short strlen = super.readShort();
        if ( strlen > 0 ) {
            byte[] bytebuf = new byte[ strlen ];
            super.readFully( bytebuf );
            // return ( new String( bytebuf ) ).trim();
            return ( new String( bytebuf ) );
        }
        else
            return "";
    }

    public String readStringWithLimit( short max_strlen )
    throws java.io.IOException
    {
        short strlen = super.readShort();
        if ( strlen > 0 ) {
            if ( strlen > max_strlen )
                strlen = max_strlen;
            byte[] bytebuf = new byte[ strlen ];
            super.readFully( bytebuf );
            // return ( new String( bytebuf ) ).trim();
            return ( new String( bytebuf ) );
        }
        else
            return "";
    }
}

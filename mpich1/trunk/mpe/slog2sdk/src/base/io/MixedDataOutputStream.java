/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.OutputStream;
import java.io.DataOutputStream;

public class MixedDataOutputStream extends DataOutputStream
                                   implements MixedDataOutput
{
    public MixedDataOutputStream( OutputStream outs )
    {
        super( outs );
    }

    public void writeString( String str )
    throws java.io.IOException
    {
        byte[] bytebuf = str.getBytes();
        short strlen = (short) bytebuf.length;
        super.writeShort( strlen );
        super.write( bytebuf );
    }
}

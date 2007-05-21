/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.ByteArrayOutputStream;

/*
   The goal of BufArrayOutputStream is to provide a handle of the
   internal byte array buffer used in ByteArrayOutputStream(
   getByteArrayBuf() ) to avoid extra byte array creation( toByteArray() )
   System.arraycopy()
*/
public class BufArrayOutputStream extends ByteArrayOutputStream
{
    public BufArrayOutputStream( int size )
    {
        super( size );
    }

    public byte[] getByteArrayBuf()
    {
        return super.buf;
    }
}

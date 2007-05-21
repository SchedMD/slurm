/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.io;

// import java.io.ByteArrayOutputStream;
import java.io.ByteArrayInputStream;

/*
   This class is meant to be extended such that the
   ByteArray{Input|Output} streams can be used to create
   Data{Input|Output} streams, e.g.

        output     = new MixedDataOutputStream( bary_outs );
        input      = new MixedDataInputStream( bary_ins );

   Hence output and input can be used as the in and out of the pipe.
*/
public class BufArrayPipedStream
{
    private   int                    buf_size;
    protected BufArrayOutputStream   bary_outs;
    protected ByteArrayInputStream   bary_ins;

    public BufArrayPipedStream()
    {
        buf_size   = 0;
        bary_outs  = null;
        bary_ins   = null;
    }

    // size  determines the size of underlying byte[] buffer
    public BufArrayPipedStream( int size )
    {
        this.resizeBuffer( size );
    }

    public void resizeBuffer( int size )
    {
        if ( size <= 0 )
            return;

        buf_size   = size;
        // Synchronize bary_outs and bary_ins to use the same byte[] buffer.
        bary_outs  = new BufArrayOutputStream( buf_size );
        bary_ins   = new ByteArrayInputStream( bary_outs.getByteArrayBuf() );
    }

    public int getBufferSize()
    {
        return  buf_size;
    }

    public void reset()
    {
        bary_ins.reset();
        bary_outs.reset();
    }

    public void close()
    throws java.io.IOException
    {}
}

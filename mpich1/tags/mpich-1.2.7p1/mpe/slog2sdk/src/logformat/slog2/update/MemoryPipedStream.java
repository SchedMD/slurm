/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package logformat.slog2.update;

import base.io.BufArrayPipedStream;
import base.io.MixedDataInputStream;

public class MemoryPipedStream extends BufArrayPipedStream
{
    public old_base.io.MixedDataOutputStream   output;
    public MixedDataInputStream                input;

    public MemoryPipedStream( int size )
    {
        this.resizeBuffer( size );
    }

    public void resizeBuffer( int size )
    {
        super.resizeBuffer( size );
        // Create Data{Input|Output} to use the ByteArray{Input|Output} streams.
        output = new old_base.io.MixedDataOutputStream( super.bary_outs );
        input  = new MixedDataInputStream( super.bary_ins );
    }

    public void close()
    throws java.io.IOException
    {
        super.close();
        input.close();
        output.close();
    }

    public final static void main( String[] argv )
    {
        MemoryPipedStream  mem_pipe  = new MemoryPipedStream( 48 );

        try {
            for ( int idx = 0; idx < 100; idx++ ) {
                // The memory stream needs to be reset every time,
                // otherwise the whole memory buffer could be used up.
                mem_pipe.reset();

                mem_pipe.output.writeInt( 3 + (int) idx );
                mem_pipe.output.writeDouble( 10.0 + (double) idx );
                mem_pipe.output.writeShort( 4 + (short) idx );
                mem_pipe.output.writeShort( 9 + (short) idx );
                mem_pipe.output.writeInt( 2 + (int) idx );
                mem_pipe.output.writeString( "Hello People!" );

                int    iia = mem_pipe.input.readInt();
                System.out.println( "iia = " + iia );
                double dfa = mem_pipe.input.readDouble();
                System.out.println( "dfa = " + dfa );
                short  isa = mem_pipe.input.readShort();
                System.out.println( "isa = " + isa );
                short  isb = mem_pipe.input.readShort();
                System.out.println( "isb = " + isb );
                int    iib = mem_pipe.input.readInt();
                System.out.println( "iib = " + iib );
                String str = mem_pipe.input.readString();
                System.out.println( "str = " + str );

                System.out.println();
            }
            mem_pipe.close();
        } catch ( java.io.IOException ioerr ) {
            ioerr.printStackTrace();
        }
    }
}

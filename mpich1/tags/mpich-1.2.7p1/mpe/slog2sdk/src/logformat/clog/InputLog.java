/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;

public class InputLog
{
    private static final int    BLOCKSIZE = Const.BLOCK_SIZE;
    private String              filename;
    private DataInputStream     main_ins;
    private byte[]              buffer;

    public InputLog( String pathname )
    {
        FileInputStream fins;
        BufferedInputStream buf_ins;

        filename = pathname;
        fins = null;
        try {
            fins = new FileInputStream( filename );
        } catch ( FileNotFoundException ferr ) {
            ferr.printStackTrace();
            System.exit( 0 );
        }
        buf_ins  = new BufferedInputStream( fins, BLOCKSIZE );

        main_ins = new DataInputStream( buf_ins );
        buffer   = null;
    }

    public String getFileName()
    {
        return filename;
    }

    public MixedDataInputStream getBlockStream()
    {
        if ( main_ins == null ) {
            System.err.println( "Error: input_stream.main_ins == null !!" );
            return null;
        }

        if ( buffer == null )
            buffer = new byte[ BLOCKSIZE ];

        try {
            main_ins.readFully( buffer );
        } catch ( EOFException eof ) {
            return null;
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return null;
        }

        return new MixedDataInputStream( new ByteArrayInputStream( buffer ) );
    }

    public void close()
    {
        try {
            main_ins.close();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
        }
    }

    protected void finalize() throws Throwable
    {
        try {
            close();
        } finally {
            super.finalize();
        }
    }
}

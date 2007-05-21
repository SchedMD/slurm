/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.util.List;
import java.io.*;

public class InputLog
{
    private static final int    INPUT_STREAM_BUFSIZE = 1024;
    private String              filename;
    private DataInputStream     main_ins;
    private Preamble            preamble;
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
        buf_ins  = new BufferedInputStream( fins, INPUT_STREAM_BUFSIZE );

        main_ins = new DataInputStream( buf_ins );

        preamble = new Preamble();
        preamble.readFromDataStream( main_ins );
        if ( ! preamble.isVersionMatched() ) {
            if ( ! preamble.isVersionCompatible() ) {
                System.err.println( "Error: CLOG versions mismatched !\n"
                                  + "\t" + "The input logfile version is "
                                  + preamble.getVersionString() + "\n"
                                  + "\t" + "But this tool is of version "
                                  + Const.VERSION );
                System.exit( 1 );
            }
            else {
                System.err.println( "Warning: CLOG versions compatible !\n"
                                  + "\t" + "The input logfile version is "
                                  + preamble.getVersionString() + "\n"
                                  + "\t" + "But this tool is of version "
                                  + Const.VERSION );
            }
        }
        if ( ! preamble.isBigEndian() ) {
            System.err.println( "Error: input logfile is little-endian!" );
            System.exit( 1 );
        }

        buffer   = null;
    }

    public String getFileName()
    {
        return filename;
    }

    public Preamble getPreamble()
    {
        return preamble;
    }

    public MixedDataInputStream getBlockStream()
    {
        if ( main_ins == null ) {
            System.err.println( "Error: input_stream.main_ins == null !!" );
            return null;
        }

        if ( buffer == null )
            buffer = new byte[ preamble.getBlockSize() ];

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

    public List getKnownUndefinedInitedStateDefs()
    {
        return RecDefState.getUndefinedInitedStateDefs(
                           preamble.getKnownEventIDStart(),
                           preamble.getKnownStateIDCount() );
    }

    public List getUserUndefinedInitedStateDefs()
    {
        return RecDefState.getUndefinedInitedStateDefs(
                           preamble.getUserEventIDStart(),
                           preamble.getUserStateIDCount() );
    }

    public List getUserUndefinedInitedEventDefs()
    {
        return RecDefEvent.getUndefinedInitedEventDefs(
                           preamble.getUserSoloEventIDStart(),
                           preamble.getUserSoloEventIDCount() );
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

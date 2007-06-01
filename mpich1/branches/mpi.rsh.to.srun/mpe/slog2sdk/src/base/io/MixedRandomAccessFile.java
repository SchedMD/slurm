/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.*;

// Extend java.io.RandomAccessFile to include readString() and writeString()
public class MixedRandomAccessFile extends RandomAccessFile
                                   implements MixedDataOutput, MixedDataInput
{
    public MixedRandomAccessFile( File file, String mode )
    // throws FileNotFoundException
    throws IOException
    {
        super( file, mode );
    }

    public MixedRandomAccessFile( String name, String mode )
    // throws FileNotFoundException
    throws IOException
    {
        super( name, mode );
    }

    public static int getStringByteSize( String str )
    {
        return str.length() + 2;
    }

    public void writeString( String str )
    throws IOException
    {
        byte[] bytebuf = str.getBytes();
        short strlen = (short) bytebuf.length;
        super.writeShort( strlen );
        super.write( bytebuf );
    }

    public String readString()
    throws IOException
    {
        short strlen = super.readShort();
        if ( strlen > 0 ) {
            byte[] bytebuf = new byte[ strlen ];
            super.readFully( bytebuf );
            // return ( new String( bytebuf ) ).trim();
            return ( new String( bytebuf ) );
        }
        else
            return null;
    }

    public String readStringWithLimit( short max_strlen )
    throws IOException
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
            return null;
    }

    // private static final String  version_ID     = "SLOG 2.0.0";

    public static final void main( String[] args )
    {
        MixedRandomAccessFile fin = null;
        try {
            fin = new MixedRandomAccessFile( "tmpfile", "rw" );
        // } catch ( FileNotFoundException ferr ) {
        } catch ( IOException ferr ) {
            ferr.printStackTrace();
            System.exit( 1 );
        }

	// System.out.println( "version_ID's disk-footprint = "
	//                   + getStringByteSize( version_ID ) );
        try {
            if ( args[ 0 ].trim().equals( "write" ) )
                fin.writeString( "SLOG 2.0.0" );
            if ( args[ 0 ].trim().equals( "read" ) )
                System.out.println( fin.readString() );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }
    }
}

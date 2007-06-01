/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;


// Class corresponds to CLOG_Rec_Srcloc
public class RecSrc
{
    public  static final int RECTYPE  = Const.RecType.SRCLOC;
    private static final int BYTESIZE = 2 * 4
                                      + StrFile.BYTESIZE; 
    public  int     srcloc;           // id of source location
    public  int     lineno;           // line number in source file
    public  String  filename;         // source file of log statement
  
    public int readFromDataStream( MixedDataInputStream in )
    {
        try {
            srcloc    = in.readInt();
            lineno    = in.readInt();
            filename  = in.readString( StrFile.BYTESIZE );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        return BYTESIZE;
    }

    public int skipBytesFromDataStream( DataInputStream in )
    {
        try {
            in.skipBytes( BYTESIZE );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        return BYTESIZE;
    }
  
    public String toString()
    {
        return ( "RecSrc"
               + "[ srcloc=" + srcloc
               + ", lineno=" + lineno
               + ", filename=" + filename
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }
}

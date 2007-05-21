/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;
import java.util.*;


// Class corresponds to CLOG_RAW
public class RecRaw
{
    public  static final int RECTYPE  = Const.RecType.RAWEVENT;
    private static final int BYTESIZE = 4 * 4
                                      + StrDesc.BYTESIZE;
    public         Integer etype;                      // raw event
    public         int     data;                       // uninterpreted data
    public         int     srcloc;                     // id of source location
    private static int     pad;
    public         String  string;                     // uninterpreted string

    private        int     msg_tag  = Const.INVALID_int; // MPI message tag
    private        int     msg_size = Const.INVALID_int; // MPI message size
 
    //read the record from the given input stream
    public int readFromDataStream( MixedDataInputStream in )
    {
        try {
            etype   = new Integer( in.readInt() );
            data    = in.readInt();
            srcloc  = in.readInt();
            pad     = in.readInt();
            string  = in.readString( StrDesc.BYTESIZE );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        if (    etype.intValue() == Const.MsgType.SEND
             || etype.intValue() == Const.MsgType.RECV )
            setMsgParams();

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

    // This following routine is for SEND and RECV events only
    private void setMsgParams()
    {
        StringTokenizer strs = new StringTokenizer( this.string, " " );
        if ( strs.hasMoreTokens() )
            msg_tag  = Integer.parseInt( strs.nextToken() );
        if ( strs.hasMoreTokens() )
            msg_size = Integer.parseInt( strs.nextToken() );
    }

    public int getMsgTag()
    {
        return msg_tag;
    }

    public int getMsgSize()
    {
        return msg_size;
    }
  
    //Copy Constructor
    public RecRaw copy()
    {
        RecRaw cp  = new RecRaw();
        cp.etype   = this.etype;
        cp.data    = this.data;
        cp.srcloc  = this.srcloc;
        cp.pad     = this.pad;
        cp.string  = this.string;
        return cp;
    }

    public String toString()
    {
        if (    etype.intValue() == Const.MsgType.SEND
             || etype.intValue() == Const.MsgType.RECV )
            return ( "RecRaw"
                   + "[ etype=" + etype
                   + ", data=" + data
                   + ", srcloc=" + srcloc
                   + ", msg_tag=" + msg_tag
                   + ", msg_size=" + msg_size
                   // + ", pad=" + pad
                   // + ", BYTESIZE=" + BYTESIZE
                   + " ]" );
        else
            return ( "RecRaw"
                   + "[ etype=" + etype
                   + ", data=" + data
                   + ", srcloc=" + srcloc
                   + ", string=" + string
                   // + ", pad=" + pad
                   // + ", BYTESIZE=" + BYTESIZE
                   + " ]" );
    }
}

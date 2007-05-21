/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;


// Class corresponds to CLOG_Rec_MsgEvt
public class RecMsg 
{
    public  static final int RECTYPE  = Const.RecType.MSGEVT;    
    private static final int BYTESIZE = 6 * 4;
    public         Integer   etype;       // kind of message event 
    private        int       icomm;       // remote communicator
    private        int       rank;        // remote rank, src/dest in send/recv
    public         int       tag;         // message tag 
    public         int       size;        // length in bytes 
    private static int       pad;         // byte padding

    public         int       lineID;      // lineID used in drawable
  
    public int readFromDataStream( DataInputStream in )
    {
        try {
            etype    = new Integer( in.readInt() );
            icomm    = in.readInt();
            rank     = in.readInt();
            tag      = in.readInt();
            size     = in.readInt();
            pad      = in.readInt();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        lineID   = LineID.compute( icomm, rank );

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

    private String toEventString()
    {
        switch (etype.intValue()) {
            case Const.MsgType.SEND:
                return "send";
            case Const.MsgType.RECV:
                return "recv";
            default:
                return "Unknown(" + etype + ")";
        }
    }

    public String toString()
    {
        return ( "RecMsg"
               + "[ etype=" + toEventString()
               + ", icomm=" + icomm
               + ", rank=" +  rank
               + ", tag=" + tag
               + ", size=" + size
               // + ", pad=" + pad
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }
}

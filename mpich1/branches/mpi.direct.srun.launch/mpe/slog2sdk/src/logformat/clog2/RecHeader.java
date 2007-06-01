/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;

// Class corresponds to CLOG_Rec_Header
public class RecHeader
{
    private static final int BYTESIZE = 8 + 4 * 4;
    public         double time;
    private        int    icomm;    // unique communicator ID
    private        int    rank;     // rank of communicator labelled by icomm
    public         int    thread;
    public         int    rectype;

    public         int    lineID;   // lineID used in drawable

    public RecHeader()
    {
        time       = Const.INVALID_double;
        icomm      = Const.INVALID_int;
        rank       = Const.INVALID_int;
        thread     = Const.INVALID_int;
        rectype    = Const.INVALID_int;

        lineID     = Const.INVALID_int;
    }

    public RecHeader( DataInputStream istm )
    {
        this.readFromDataStream( istm );
    }

    public int readFromDataStream( DataInputStream istm )
    {
        try {
            time       = istm.readDouble();
            icomm      = istm.readInt();
            rank       = istm.readInt();
            thread     = istm.readInt();
            rectype    = istm.readInt();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        lineID  = LineID.compute( icomm, rank );

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

/*
    //Copy Constructor
    public RecHeader copy()
    {
        RecHeader cp  = new RecHeader();
        cp.time       = this.time; 
        cp.icomm      = this.icomm;
        cp.rank       = this.rank;
        cp.thread     = this.thread;
        cp.rectype    = this.rectype;

        lineID  = LineID.compute( icomm, rank );
        return cp;
    }
*/

    public int getRecType()
    {
        return this.rectype;
    }

    public String toString()
    {
        return ( "RecHeader"
               + "[ time=" + time
               + ", icomm=" + icomm
               + ", rank=" + rank
               + ", thread=" + thread 
               + ", rectype=" + rectype
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    } 
}

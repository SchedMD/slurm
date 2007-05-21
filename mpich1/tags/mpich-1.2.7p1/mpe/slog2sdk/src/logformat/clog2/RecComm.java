/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;


// Class corresponds to CLOG_Rec_CommEvt
public class RecComm
{
    public  static final int RECTYPE  = Const.RecType.COMMEVT;
    private static final int BYTESIZE = 4 * 4 + UUID.BYTESIZE;
    public         Integer   etype;   // type of communicator creation
    public         int       icomm;   // created comm's ID
    public         int       rank;    // icomm rank of the process
    public         int       wrank;   // MPI_COMM_WORLD rank of the process
    public         UUID      gcomm;   // icomm's UUID

    public         int       lineID;  // lineID used in drawable

    public int readFromDataStream( DataInputStream in )
    {
        try {
            etype     = new Integer( in.readInt() );
            icomm     = in.readInt();
            rank      = in.readInt();
            wrank     = in.readInt();
            gcomm     = new UUID();
            gcomm.readFromDataStream( in );
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

    public static String toCommTypeString( int commtype )
    {
        switch (commtype) {
            case Const.CommType.WORLD_CREATE:
                return "CommWorldCreate";
            case Const.CommType.SELF_CREATE:
                return "CommSelfCreate";
            case Const.CommType.FREE:
                return "CommFree";
            case Const.CommType.INTRA_CREATE:
                return "IntraCommCreate";
            case Const.CommType.INTRA_LOCAL:
                return "LocalIntraComm";
            case Const.CommType.INTRA_REMOTE:
                return "RemoteIntraComm";
            case Const.CommType.INTER_CREATE:
                return "InterCommCreate";
            default:
                return "Unknown(" + commtype + ")";
        }
    }

    public String toString()
    {
        return ( "RecComm"
               + "[ etype=" + toCommTypeString( etype.intValue() )
               + ", icomm=" + icomm
               + ", rank=" + rank
               + ", wrank=" + wrank
               + ", gcomm=" + gcomm
               + " ]");
    }
}

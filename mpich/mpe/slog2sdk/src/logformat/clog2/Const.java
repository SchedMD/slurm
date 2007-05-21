/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

public class Const
{
    // Current Version ID
           static final String    VERSION         = "CLOG-02.40";

    /*
       Older Version IDs, COMPAT_VERSIONS, that are compataible to VERSION.
       Version 02.20 is compatible with 02.10.
       If No compatibel version, define COMPAT_VERSIONS as follows
           static final String[]  COMPAT_VERSIONS = {};
       If VERSION is compatible with CLOG-02.10, define COMPAT_VERSIONS as
           static final String[]  COMPAT_VERSIONS = { "CLOG-02.10" };
       VERSION 2.30 updates CLOG2 format to supports MPI_Comm.
       VERSION 2.40 updates clog2TOdrawable to support user-defined events,
                    and updates CLOG2's preamble to contain various
                    eventID and stateID info.
    */
           static final String[]  COMPAT_VERSIONS = {};

           static final byte      INVALID_byte    = Byte.MIN_VALUE;
           static final short     INVALID_short   = Short.MIN_VALUE;
           static final int       INVALID_int     = Integer.MIN_VALUE;
           static final long      INVALID_long    = Long.MIN_VALUE;
           static final int       NULL_int        = 0;
           static final int       NULL_iaddr      = 0;
           static final long      NULL_fptr       = 0;
           static final float     INVALID_float   = Float.MIN_VALUE;
           static final double    INVALID_double  = Double.MIN_VALUE;
           static final int       TRUE            = 1;
           static final int       FALSE           = 0;

    public class AllType
    {
        public static final int   UNDEF     =  -1;     // CLOG_REC_UNDEF
    }

    public class RecType
    {
        // public static final int   UNDEF     =  -1;     // CLOG_REC_UNDEF
        public static final int   ENDLOG    =  0;      // CLOG_REC_ENDLOG
        public static final int   ENDBLOCK  =  1;      // CLOG_REC_ENDBLOCK
               static final int   STATEDEF  =  2;      // CLOG_REC_STATEDEF
               static final int   EVENTDEF  =  3;      // CLOG_REC_EVENTDEF
               static final int   CONSTDEF  =  4;      // CLOG_REC_CONSTDEF
               static final int   BAREEVT   =  5;      // CLOG_REC_BAREEVT
               static final int   CARGOEVT  =  6;      // CLOG_REC_CARGOEVT
               static final int   MSGEVT    =  7;      // CLOG_REC_MSGEVT
               static final int   COLLEVT   =  8;      // CLOG_REC_COLLEVT
               static final int   COMMEVT   =  9;      // CLOG_REC_COMMEVT
               static final int   SRCLOC    = 10;      // CLOG_REC_SRCLOC
               static final int   SHIFT     = 11;      // CLOG_REC_TIMESHIFT
    }

    class MsgType // define send and recv events
    {
               static final int   SEND      = -101;    // CLOG_EVT_SENDMSG
               static final int   RECV      = -102;    // CLOG_EVT_RECVMSG
    }

    class CommType // define CLOG communicator event types
    {
               static final int   WORLD_CREATE =    0; // CLOG_COMM_WORLD_CREATE
               static final int   SELF_CREATE  =    1; // CLOG_COMM_SELF_CREATE 
               static final int   FREE         =   10; // CLOG_COMM_FREE        
               static final int   INTRA_CREATE =  100; // CLOG_COMM_INTRA_CREATE
               static final int   INTRA_LOCAL  =  101; // CLOG_COMM_INTRA_LOCAL 
               static final int   INTRA_REMOTE =  102; // CLOG_COMM_INTRA_REMOTE
               static final int   INTER_CREATE = 1000; // CLOG_COMM_INTER_CREATE
    }
}

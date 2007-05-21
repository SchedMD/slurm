/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.io.*;


// Class corresponds to CLOG_STATE
public class RecDefState
{
    public  final static int RECTYPE  = Const.RecType.STATEDEF;
    private final static int BYTESIZE = 4 * 4
                                      + StrDesc.BYTESIZE
                                      + StrCname.BYTESIZE;
    public         int     stateID;          // integer identifier for state
    public         Integer startetype;       // starting event for state 
    public         Integer endetype;         // ending event for state 
    private static int     pad;
    public         String  color;            // string for color
    public         String  description;      // string describing state
  
    //read the record from the given input stream
    public int readFromDataStream( MixedDataInputStream in )
    {
        try {
            stateID      = in.readInt();
            startetype   = new Integer( in.readInt() );
            endetype     = new Integer( in.readInt() );
            pad          = in.readInt();
            color        = in.readString( StrCname.BYTESIZE );
            description  = in.readString( StrDesc.BYTESIZE );
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

    // Mimic <MPE>/src/log_wrap.c's definition of MPI_Init() in initialization
    // of states[] and the way MPI_xxx routines's CLOG_STATE is defined
    // in MPI_Finalize();
    public static List getMPIinitUndefinedStateDefs()
    {
        RecDefState def;

        // evtID is equivalent to the variable "stateid" MPI_Init() 
        // in <MPE>/src/log_wrap.c
        int evtID = Const.MPE_1ST_EVENT;  
        List defs = new ArrayList( Const.MPE_MAX_STATES );
        for ( int idx = 0; idx < Const.MPE_MAX_STATES; idx++ ) {
            def              = new RecDefState();
            def.startetype   = new Integer( evtID++ );
            def.endetype     = new Integer( evtID++ );
            def.color        = "pink";
            def.description  = null;
            defs.add( def );
        }
        return defs;
    }
  
    public static List getUSERinitUndefinedStateDefs()
    {
        RecDefState def;

        // evtID is equivalent to the variable "stateid" MPI_Init() 
        // in <MPE>/src/log_wrap.c
        int evtID = Const.MPE_USER_1ST_EVENT;  
        List defs = new ArrayList( Const.MPE_USER_MAX_STATES );
        for ( int idx = 0; idx < Const.MPE_USER_MAX_STATES; idx++ ) {
            def              = new RecDefState();
            def.startetype   = new Integer( evtID++ );
            def.endetype     = new Integer( evtID++ );
            def.color        = "pink";
            def.description  = null;
            defs.add( def );
        }
        return defs;
    }
  
    public String toString()
    { 
        return ( "RecDefState"
               + "[ stateID=" + stateID
               + ", startetype=" + startetype
               + ", endetype=" + endetype
               + ", pad=" + pad
               + ", color=" + color
               + ", description=" + description
               // + ", BYTESIZE=" + BYTESIZE
               + " ]" );
    }

    public static final void main( String[] args )
    {
        List defs = RecDefState.getMPIinitUndefinedStateDefs();
        Iterator itr = defs.iterator();
        while ( itr.hasNext() )
            System.out.println( (RecDefState) itr.next() );
    }
}

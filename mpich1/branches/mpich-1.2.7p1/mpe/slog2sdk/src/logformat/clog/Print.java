/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

import java.io.*;

// This program prints the CLOG file format.
public class Print
{
    public static final void main( String[] args )
    {
        if ( args.length != 1 ) {
            System.err.println( "It needs the filename to be the only command "
                              + "line arguemnt" );
            System.exit( 0 );
        }

        InputLog clog_ins;
        MixedDataInputStream blk_ins;
        int total_bytesize, bytes_read;
        int rectype;

        clog_ins = new InputLog( args[ 0 ] );

        RecHeader     header    = new RecHeader();
        RecDefState   statedef  = new RecDefState();
        RecRaw        raw       = new RecRaw();
        RecColl       coll      = new RecColl();
        RecComm       comm      = new RecComm();
        RecEvent      event     = new RecEvent();
        RecMsg        msg       = new RecMsg();
        RecSrc        src       = new RecSrc();
        RecTshift     tshift    = new RecTshift();

        total_bytesize = 0;
        while ( ( blk_ins = clog_ins.getBlockStream() ) != null ) {
            rectype = Const.AllType.UNDEF;
            while (  rectype != Const.RecType.ENDBLOCK
                  && rectype != Const.RecType.ENDLOG ) {
                bytes_read = header.readFromDataStream( blk_ins );
                System.out.println( header.toString() );
                total_bytesize += bytes_read;
    
                rectype = header.getRecType();
                switch ( rectype ) {
                    case RecDefState.RECTYPE:
                        bytes_read = statedef.readFromDataStream( blk_ins );
                        System.out.println( statedef.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case RecRaw.RECTYPE:
                        bytes_read = raw.readFromDataStream( blk_ins );
                        System.out.println( raw.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case RecColl.RECTYPE:
                        bytes_read = coll.readFromDataStream( blk_ins );
                        System.out.println( coll.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case RecComm.RECTYPE:
                        bytes_read = comm.readFromDataStream( blk_ins );
                        System.out.println( comm.toString() );
                        total_bytesize += bytes_read; 
                        break;
                    case RecEvent.RECTYPE:
                        bytes_read = event.readFromDataStream( blk_ins );
                        System.out.println( event.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case RecMsg.RECTYPE:
                        bytes_read = msg.readFromDataStream( blk_ins );
                        System.out.println( msg.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case RecSrc.RECTYPE:
                        bytes_read = src.readFromDataStream( blk_ins );
                        System.out.println( src.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case RecTshift.RECTYPE:
                        bytes_read = tshift.readFromDataStream( blk_ins );
                        System.out.println( tshift.toString() );
                        total_bytesize += bytes_read;
                        break;
                    case Const.RecType.ENDBLOCK:
                        System.out.println( "End Of Block" );
                        break;
                    case Const.RecType.ENDLOG:
                        System.out.println( "End Of File" );
                        break;
                    default:
                        System.err.println( "Unknown Record type = "
                                          + rectype );
                }   // endof switch ( rectype )
            }   //  endof while ( rectype != (ENDBLOCK/ENDLOG) )
        }   //  endof while ( getBlockStream() )

        clog_ins.close();
        System.out.println( "Total ByteSize of the logfile = "
                          + total_bytesize );
    }
}

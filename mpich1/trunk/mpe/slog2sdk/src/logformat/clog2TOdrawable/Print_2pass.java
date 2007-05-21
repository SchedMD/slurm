/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Date;
import java.io.*;
import java.lang.reflect.*;

import base.drawable.Primitive;
import logformat.clog2.*;



// This program prints the CLOG file format.
public class Print_2pass
{
    private static String                   filename;
    private static logformat.clog2.InputLog clog_ins;
    private static MixedDataInputStream     blk_ins;
    private static int                      total_bytesize;
    private static int                      bytes_read;
    private static int                      rectype;
    private static Map                      evtdefs;
    private static List                     objdefs;
    private static ObjDef                   objdef;

    private static RecHeader                header    = new RecHeader();
    private static RecDefState              staterec  = new RecDefState();
    private static RecDefEvent              eventrec  = new RecDefEvent();
    private static RecDefConst              constrec  = new RecDefConst();
    private static RecBare                  bare      = new RecBare();
    private static RecCargo                 cargo     = new RecCargo();
    private static RecMsg                   msg       = new RecMsg();
    private static RecColl                  coll      = new RecColl();
    private static RecComm                  comm      = new RecComm();
    private static RecSrc                   src       = new RecSrc();
    private static RecTshift                tshift    = new RecTshift();

    private static Topo_Arrow               arrowform;
    private static Topo_State               stateform;
    private static ObjMethod                obj_fn;

    public static final void createDefs()
    {
        Class           arrow_class, state_class;
        Method          start_fn = null, final_fn = null;
        Class[]         typelist;

        clog_ins  = new logformat.clog2.InputLog( filename );
        objdefs   = new ArrayList();
        evtdefs   = new HashMap();
        ColorNameMap.initMapFromRGBtxt( "jumpshot.colors" );
        ObjDef.setFirstNextCategoryIndex( 0 );

        int def_idx;
        arrowform = new Topo_Arrow();
        def_idx = ObjDef.getNextCategoryIndex();
        objdef = new ObjDef( def_idx, new RecDefMsg(), arrowform, 3 );
        arrowform.setCategory( objdef );
        objdefs.add( objdef.getIndex(), objdef );
        arrow_class = arrowform.getClass();
        typelist = new Class[] { RecHeader.class, RecMsg.class };
        try {
            start_fn = arrow_class.getMethod( "matchStartEvent", typelist );
            final_fn = arrow_class.getMethod( "matchFinalEvent", typelist );
        } catch ( NoSuchMethodException err ) {
             err.printStackTrace();
             System.exit( 1 );
        }

        obj_fn = new ObjMethod();
        obj_fn.obj = arrowform;
        obj_fn.method = start_fn;
        evtdefs.put( objdef.start_evt, obj_fn );

        obj_fn = new ObjMethod();
        obj_fn.obj = arrowform;
        obj_fn.method = final_fn;
        evtdefs.put( objdef.final_evt, obj_fn );

        total_bytesize = 0;
        typelist = new Class[] { RecHeader.class, RecBare.class };
        while ( ( blk_ins = clog_ins.getBlockStream() ) != null ) {
            rectype = logformat.clog2.Const.AllType.UNDEF;
            while (  rectype != logformat.clog2.Const.RecType.ENDBLOCK
                  && rectype != logformat.clog2.Const.RecType.ENDLOG ) {
                bytes_read = header.readFromDataStream( blk_ins );
                total_bytesize += bytes_read;
    
                rectype = header.getRecType();
                switch ( rectype ) {
                    case RecDefState.RECTYPE:
                        bytes_read = staterec.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        stateform = new Topo_State();
                        def_idx = ObjDef.getNextCategoryIndex();
                        objdef = new ObjDef( def_idx, staterec, stateform, 1 );
                        stateform.setCategory( objdef );
                        objdefs.add( objdef.getIndex(), objdef );
                        state_class = stateform.getClass();
                        try {
                            start_fn = state_class.getMethod( "matchStartEvent",
                                                              typelist );
                            final_fn = state_class.getMethod( "matchFinalEvent",
                                                              typelist );
                        } catch ( NoSuchMethodException err ) {
                            err.printStackTrace();
                            System.exit( 1 );
                        }

                        obj_fn = new ObjMethod();
                        obj_fn.obj = stateform;
                        obj_fn.method = start_fn;
                        evtdefs.put( objdef.start_evt, obj_fn );

                        obj_fn = new ObjMethod();
                        obj_fn.obj = stateform;
                        obj_fn.method = final_fn;
                        evtdefs.put( objdef.final_evt, obj_fn );
                        break;
                    case RecDefEvent.RECTYPE:
                        bytes_read
                        = eventrec.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecDefConst.RECTYPE:
                        bytes_read
                        = constrec.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecBare.RECTYPE:
                        bytes_read = bare.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecCargo.RECTYPE:
                        bytes_read = cargo.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecMsg.RECTYPE:
                        bytes_read = msg.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecColl.RECTYPE:
                        bytes_read = coll.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecComm.RECTYPE:
                        bytes_read = comm.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read; 
                        break;
                    case RecSrc.RECTYPE:
                        bytes_read = src.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecTshift.RECTYPE:
                        bytes_read = tshift.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case logformat.clog2.Const.RecType.ENDBLOCK:
                        // System.out.println( "End Of Block" );
                        break;
                    case logformat.clog2.Const.RecType.ENDLOG:
                        // System.out.println( "End Of File" );
                        break;
                    default:
                        System.err.println( "Unknown Record type = " + rectype );
                }   // endof switch ( rectype )
            }   //  endof while ( rectype != (ENDBLOCK/ENDLOG) )
        }   //  endof while ( getBlockStream() )

        clog_ins.close();

        System.err.println( "\n\t objdefs : " );
        for ( int idx = 0; idx < objdefs.size(); idx++ ) {
            objdef = (ObjDef) objdefs.get( idx );
            System.err.println( objdef.toString() );
        }

        /*
        System.err.println( "\n\t evtdefs : " );
        Iterator evtdefs_itr = evtdefs.entrySet().iterator();
        while ( evtdefs_itr.hasNext() )
            System.err.println( evtdefs_itr.next() );
        */
    }

    public static final void createPrimitives()
    {
        Object[]        arglist;
        ObjMethod       evt_pairing;
        Primitive       drawobj;
        int             bare_etype, cargo_etype, msg_etype;
        int             Nmatched = 0;

        arglist   = new Object[ 2 ];

        System.out.println( "\n\t Completed Objects : " );
        clog_ins  = new logformat.clog2.InputLog( filename );

        total_bytesize = 0;
        while ( ( blk_ins = clog_ins.getBlockStream() ) != null ) {
            rectype = logformat.clog2.Const.AllType.UNDEF;
            while (  rectype != logformat.clog2.Const.RecType.ENDBLOCK
                  && rectype != logformat.clog2.Const.RecType.ENDLOG ) {
                bytes_read = header.readFromDataStream( blk_ins );
                total_bytesize += bytes_read;
    
                rectype = header.getRecType();
                switch ( rectype ) {
                    case RecDefState.RECTYPE:
                        bytes_read
                        = staterec.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecDefEvent.RECTYPE:
                        bytes_read
                        = eventrec.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecDefConst.RECTYPE:
                        bytes_read
                        = constrec.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecBare.RECTYPE:
                        bytes_read = bare.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        bare_etype = bare.etype.intValue();
                        if ( bare_etype != Const.AllType.UNDEF ) {
                            evt_pairing = (ObjMethod) evtdefs.get( bare.etype );
                            // Object[] arglist = new Object[] { header, bare };
                            arglist[ 0 ] = header;
                            arglist[ 1 ] = null;
                            drawobj = null;
                            try {
                                drawobj = (Primitive) evt_pairing.method
                                                     .invoke( evt_pairing.obj,
                                                              arglist );
                            } catch ( IllegalAccessException err ) {
                                err.printStackTrace();
                                System.err.println( "Offending RecBare = "
                                                  + bare );
                            // catching NoMatchingEventException
                            } catch ( InvocationTargetException err ) {
                                err.printStackTrace();
                            } catch ( NullPointerException nullerr ) {
                                nullerr.printStackTrace();
                                System.err.println( "Offending RecHeader = "
                                                  + header );
                                System.err.println( "Offending RecBare = "
                                                  + bare );
                                System.exit(1);
                            }

                            if ( drawobj != null )
                                System.out.println( (Nmatched++) + ", "
                                                  + drawobj.toString() );
                        }

                        break;
                    case RecCargo.RECTYPE:
                        bytes_read = cargo.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        cargo_etype = cargo.etype.intValue();
                        if ( cargo_etype != Const.AllType.UNDEF ) {
                            evt_pairing = (ObjMethod)
                                          evtdefs.get( cargo.etype );
                            // arglist = new Object[] { header, cargo };
                            arglist[ 0 ] = header;
                            arglist[ 1 ] = cargo;
                            drawobj = null;
                            try {
                                drawobj = (Primitive) evt_pairing.method
                                                     .invoke( evt_pairing.obj,
                                                              arglist );
                            } catch ( IllegalAccessException err ) {
                                err.printStackTrace();
                                System.err.println( "Offending RecCargo = "
                                                  + cargo );
                            // catching NoMatchingEventException
                            } catch ( InvocationTargetException err ) {
                                err.printStackTrace();
                            } catch ( NullPointerException nullerr ) {
                                nullerr.printStackTrace();
                                System.err.println( "Offending RecHeader = "
                                                  + header );
                                System.err.println( "Offending RecCargo = "
                                                  + cargo );
                                System.exit(1);
                            }

                            if ( drawobj != null )
                                System.out.println( (Nmatched++) + ", "
                                                  + drawobj.toString() );
                        }
                        break;
                    case RecMsg.RECTYPE:
                        bytes_read = msg.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        msg_etype = msg.etype.intValue();
                        if ( msg_etype != Const.AllType.UNDEF ) {
                            evt_pairing = (ObjMethod) evtdefs.get( msg.etype );
                            // Object[] arglist = new Object[] { header, msg };
                            arglist[ 0 ] = header;
                            arglist[ 1 ] = msg;
                            drawobj = null;
                            try {
                                drawobj = (Primitive) evt_pairing.method
                                                     .invoke( evt_pairing.obj,
                                                              arglist );
                            } catch ( IllegalAccessException err ) {
                                err.printStackTrace();
                                System.err.println( "Offending RecMsg = "
                                                  + msg );
                            // catching NoMatchingEventException
                            } catch ( InvocationTargetException err ) {
                                err.printStackTrace();
                            } catch ( NullPointerException nullerr ) {
                                nullerr.printStackTrace();
                                System.err.println( "Offending RecHeader = "
                                                  + header );
                                System.err.println( "Offending RecMsg = "
                                                  + msg );
                                System.exit(1);
                            }

                            if ( drawobj != null )
                                System.out.println( (Nmatched++) + ", "
                                                  + drawobj.toString() );
                        }
                        break;
                    case RecColl.RECTYPE:
                        bytes_read = coll.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecComm.RECTYPE:
                        bytes_read = comm.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read; 
                        break;
                    case RecSrc.RECTYPE:
                        bytes_read = src.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecTshift.RECTYPE:
                        bytes_read = tshift.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case logformat.clog2.Const.RecType.ENDBLOCK:
                        // System.out.println( "End Of Block" );
                        break;
                    case logformat.clog2.Const.RecType.ENDLOG:
                        // System.out.println( "End Of File" );
                        break;
                    default:
                        System.err.println( "Unknown Record type = "
                                          + rectype );
                }   // endof switch ( rectype )
            }   //  endof while ( rectype != (ENDBLOCK/ENDLOG) )
        }   //  endof while ( getBlockStream() )

        clog_ins.close();

        System.err.println( "\n\t " + arrowform.getPartialObjects().size()
                          + " Unmatched arrow events" );
        System.err.println( "\n\t " + stateform.getPartialObjects().size()
                          + " Unmatched state events" );
    }

    public static final void main( String[] args )
    {
        if ( args.length != 1 ) {
            System.err.println( "It needs the filename to be the only command "
                              + "line arguemnt" );
            System.exit( 0 );
        }

        filename = args[ 0 ];

        Date time1 = new Date();
        createDefs();
        Date time2 = new Date();
        createPrimitives();
        Date time3 = new Date();

        System.err.println( "Total ByteSize of the logfile = "
                          + total_bytesize );
        // System.err.println( "time1 = " + time1 + ", " + time1.getTime() );
        // System.err.println( "time2 = " + time2 + ", " + time2.getTime() );
        // System.err.println( "time3 = " + time3 + ", " + time3.getTime() );
        System.err.println( "timeElapsed between 1 & 2 = "
                          + ( time2.getTime() - time1.getTime() ) + " msec" );
        System.err.println( "timeElapsed between 2 & 3 = "
                          + ( time3.getTime() - time2.getTime() ) + " msec" );
    }
}

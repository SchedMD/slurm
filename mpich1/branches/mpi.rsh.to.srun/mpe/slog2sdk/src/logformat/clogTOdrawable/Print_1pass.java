/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clogTOdrawable;

import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Date;
import java.lang.reflect.*;
import java.io.*;

import base.drawable.Primitive;
import logformat.clog.*;



// This program prints the CLOG file format.
public class Print_1pass
{
    private static String                   filename;
    private static logformat.clog.InputLog  clog_ins;
    private static MixedDataInputStream     blk_ins;
    private static int                      total_bytesize;
    private static int                      bytes_read;
    private static int                      rectype;
    private static Map                      evtdefs;
    private static List                     objdefs;
    private static ObjDef                   objdef;

    private static RecHeader                header    = new RecHeader();
    private static RecDefState              def       = new RecDefState();
    private static RecRaw                   raw       = new RecRaw();
    private static RecColl                  coll      = new RecColl();
    private static RecComm                  comm      = new RecComm();
    private static RecEvent                 event     = new RecEvent();
    private static RecMsg                   msg       = new RecMsg();
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

        objdefs   = new ArrayList();
        evtdefs   = new HashMap();
        ColorNameMap.initMapFromRGBtxt( "jumpshot.colors" );
        ObjDef.setFirstNextCategoryIndex( 0 );

        int def_idx;
        arrowform = new Topo_Arrow( );
        def_idx = ObjDef.getNextCategoryIndex();
        objdef = new ObjDef( def_idx, new RecDefMsg(), arrowform, 3 );
        objdef.setInfoKeys( "(msg_tag=%d, msg_size=%d)" );
        arrowform.setCategory( objdef );
        objdefs.add( objdef.getIndex(), objdef );
        arrow_class = arrowform.getClass();
        typelist = new Class[] { RecHeader.class, RecRaw.class };
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

        List defs = logformat.clog.RecDefState.getMPIinitUndefinedStateDefs();
        defs.addAll(
             logformat.clog.RecDefState.getUSERinitUndefinedStateDefs() );
        Iterator itr = defs.iterator();
        while ( itr.hasNext() ) {
            def = ( RecDefState ) itr.next();

            stateform = new Topo_State();
            def_idx = ObjDef.getNextCategoryIndex();
            objdef = new ObjDef( def_idx, def, stateform, 1 );
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
        }

        /*
        System.err.println( "\n\t objdefs : " );
        Iterator objdefs_itr = objdefs.iterator();
        while ( objdefs_itr.hasNext() ) {
            objdef = (obj_def) objdefs_itr.next();
            System.err.println( objdef.toString() );
        }

        System.err.println( "\n\t evtdefs : " );
        Iterator evtdefs_itr = evtdefs.entrySet().iterator();
        while ( evtdefs_itr.hasNext() )
            System.err.println( evtdefs_itr.next() );
        */
    }

    public static final void createPrimitives()
    {
        Class           state_class;
        Method          start_fn = null, final_fn = null;
        Class[]         typelist;
        ObjMethod       evt_pairing, obj_meth1, obj_meth2;
        Primitive       obj;
        int             def_idx;
        int             raw_etype;
        int             Nmatched = 0;

        typelist = new Class[] { RecHeader.class, RecRaw.class };
        System.out.println( "\n\t Completed Objects : " );
        clog_ins  = new logformat.clog.InputLog( filename );

        total_bytesize = 0;
        blk_ins = clog_ins.getBlockStream();
        while ( blk_ins != null ) {
            rectype = logformat.clog.Const.AllType.UNDEF;
            while (  rectype != logformat.clog.Const.RecType.ENDBLOCK
                  && rectype != logformat.clog.Const.RecType.ENDLOG ) {
                bytes_read = header.readFromDataStream( blk_ins );
                total_bytesize += bytes_read;
    
                rectype = header.getRecType();
                switch ( rectype ) {
                    case RecDefState.RECTYPE:
                        bytes_read = def.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        obj_meth1 = (ObjMethod) evtdefs.get( def.startetype );
                        obj_meth2 = (ObjMethod) evtdefs.get( def.endetype );
                        if ( obj_meth1 == null || obj_meth2 == null ) {
                            stateform = new Topo_State();
                            def_idx = ObjDef.getNextCategoryIndex();
                            objdef = new ObjDef( def_idx, def, stateform, 1 );
                            stateform.setCategory( objdef );
                            objdefs.add( objdef.getIndex(), objdef );
                            state_class = stateform.getClass();
                            try {
                                start_fn = state_class .getMethod(
                                           "matchStartEvent", typelist );
                                final_fn = state_class.getMethod(
                                           "matchFinalEvent", typelist );
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
                        }
                        else {  // i.e. obj_meth1 != null && obj_meth2 != null 
                            if ( obj_meth1.obj == obj_meth2.obj ) {
                                stateform = ( Topo_State ) obj_meth1.obj;
                                objdef = ( ObjDef ) stateform.getCategory();
                                objdef.setName( def.description );
                                objdef.setColor(
                                   ColorNameMap.getColorAlpha( def.color ) );
                            }
                            else {
                                System.err.println( "**** Error! "
                                                  + obj_meth1.obj + "!="
                                                  + obj_meth2.obj );
                            }
                        }
                        break;
                    case RecRaw.RECTYPE:
                        bytes_read = raw.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        raw_etype = raw.etype.intValue();
                        if (    raw_etype != Const.EvtType.CONSTDEF 
                             && raw_etype != Const.AllType.UNDEF ) {
                       	    evt_pairing = (ObjMethod) evtdefs.get( raw.etype );
                            Object[] arglist = new Object[] { header, raw };
                            obj = null;
                            try {
                                obj = (Primitive) evt_pairing.method
                                                 .invoke( evt_pairing.obj,
                                                          arglist );
                            } catch ( IllegalAccessException err ) {
                                err.printStackTrace();
                                System.err.println( "Offending rec_raw = "
                                                  + raw );
                            // catching NoMatchingEventException
                            } catch ( InvocationTargetException err ) {
                                err.printStackTrace();
                            } catch ( NullPointerException nullerr ) {
                                nullerr.printStackTrace();
                                System.err.println( "Offending rec_header = "
                                                  + header );
                                System.err.println( "Offending rec_raw = "
                                                  + raw );
                                System.exit(1);
                            }

                            if ( obj != null ) {
                                System.out.println( (++Nmatched) + ", "
                                                  + obj.toString() );
                            }
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
                    case RecEvent.RECTYPE:
                        bytes_read = event.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecMsg.RECTYPE:
                        bytes_read = msg.skipBytesFromDataStream( blk_ins );
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
                    case logformat.clog.Const.RecType.ENDBLOCK:
                        // System.out.println( "End Of Block" );
                        blk_ins = clog_ins.getBlockStream();
                        break;
                    case logformat.clog.Const.RecType.ENDLOG:
                        // System.out.println( "End Of File" );
                        blk_ins = null;
                        break;
                    default:
                        System.err.println( "Unknown Record type = "
                                          + rectype );
                }   // endof switch ( rectype )
            }   //  endof while ( rectype != (ENDBLOCK/ENDLOG) )
        }   //  endof while ( blk_ins != null )

        clog_ins.close();

        System.err.println( "\n\t " + arrowform.getPartialObjects().size()
                          + " Unmatched arrow events" );
        System.err.println( "\n\t " + stateform.getPartialObjects().size()
                          + " Unmatched state events" );

        System.err.println( "\n\t " + Nmatched + " primitives." );

        // Remove unused objdef from objdefs-list
        Iterator objdefs_itr = objdefs.iterator();
        while ( objdefs_itr.hasNext() ) {
            objdef = (ObjDef) objdefs_itr.next();
            if ( objdef.getName() == null )
                objdefs_itr.remove();
        }

        System.err.println( "\n\t objdefs : " );
        objdefs_itr = objdefs.iterator();
        while ( objdefs_itr.hasNext() ) {
            objdef = (ObjDef) objdefs_itr.next();
            System.err.println( objdef.toString() );
        }

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

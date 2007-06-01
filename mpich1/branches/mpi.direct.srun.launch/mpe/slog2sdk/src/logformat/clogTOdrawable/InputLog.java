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
import java.lang.reflect.*;
import java.io.*;

import base.drawable.*;
import logformat.clog.*;

public class InputLog extends logformat.clog.InputLog
                      implements base.drawable.InputAPI
{
    private MixedDataInputStream   blk_ins;
    private long                   total_bytesize;
    private int                    rectype;

    private Map                    evtdefs;
    private List                   topos;
    private ObjDef                 statedef;
    private ObjDef                 arrowdef;
    private Primitive              drawobj;

    private RecHeader              header;
    private RecDefState            def   ;
    private RecRaw                 raw   ;
    private RecColl                coll  ;
    private RecComm                comm  ;
    private RecEvent               event ;
    private RecMsg                 msg   ;
    private RecSrc                 src   ;
    private RecTshift              tshift;

    private Topo_Arrow             arrowform;
    private Topo_State             stateform;
    private ObjMethod              obj_fn;
    private Object[]               arglist;

    private boolean                isFirstPeekForCategory;
    private int                    num_topology_returned;

    
    public InputLog( String pathname )
    {
        super( pathname );

        // Stack event matching object function list, evtdefs.
        evtdefs   = new HashMap();

        // drawable's topology list, objdefs.
        topos = new ArrayList();

        ColorNameMap.initMapFromRGBtxt( "jumpshot.colors" );
        ObjDef.setFirstNextCategoryIndex( 0 );

        int def_idx;
        // Create the stack event matching object functions, evtdefs[],
        // for Topo_Arrow
        // Save ObjDef of arrowform to be returned by getNextCategory()
        // Set the labels for arrowdef, hence expect 2 arguments from CLOG
        arrowform = new Topo_Arrow();
        def_idx   = ObjDef.getNextCategoryIndex();
        arrowdef  = new ObjDef( def_idx, new RecDefMsg(), arrowform, 3 );
        arrowdef.setInfoKeys( "msg_tag=%d, msg_size=%d" );
        arrowdef.setColor( new ColorAlpha( arrowdef.getColor(),
                                           ColorAlpha.OPAQUE ) );
        arrowform.setCategory( arrowdef );
        evtdefs.put( arrowdef.start_evt,
                     arrowform.getStartEventObjMethod() );
        evtdefs.put( arrowdef.final_evt,
                     arrowform.getFinalEventObjMethod() );

        // Gather all the MPI and user defined undefined RecDefState's,
        // i.e. CLOG_STATE
        List defs = logformat.clog.RecDefState.getMPIinitUndefinedStateDefs();
        defs.addAll(
             logformat.clog.RecDefState.getUSERinitUndefinedStateDefs() );

        // Convert them to the appropriate categories + corresponding 
        // stack event matching object functions.
        Iterator itr = defs.iterator();
        while ( itr.hasNext() ) {
            def = ( RecDefState ) itr.next();

            stateform = new Topo_State();
            def_idx   = ObjDef.getNextCategoryIndex();
            statedef  = new ObjDef( def_idx, def, stateform, 1 );
            stateform.setCategory( statedef );
            evtdefs.put( statedef.start_evt,
                         stateform.getStartEventObjMethod() );
            evtdefs.put( statedef.final_evt,
                         stateform.getFinalEventObjMethod() );
        }

        /*
        System.err.println( "\n\t evtdefs : " );
        Iterator evtdefs_itr = evtdefs.entrySet().iterator();
        while ( evtdefs_itr.hasNext() )
            System.err.println( evtdefs_itr.next() );
        */

        // Create various CLOG records as place holders for CLOG parser.
        header    = new RecHeader();
        def       = new RecDefState();
        raw       = new RecRaw();
        coll      = new RecColl();
        comm      = new RecComm();
        event     = new RecEvent();
        msg       = new RecMsg();
        src       = new RecSrc();
        tshift    = new RecTshift();

        // Initialize argument list variable
        arglist   = new Object[ 2 ];

        // Initialize the total_bytesize read from the CLOG stream
        total_bytesize = 0;

        // Initialize Topology name return counter
        num_topology_returned = 0;

        // Initialize boolean variable, isFirstPeekForCategory
        isFirstPeekForCategory = true;

        // Initialize the CLOG block-input-stream for peekNextKind()
        blk_ins = super.getBlockStream();
    }

    public Kind peekNextKind()
    {
        ObjMethod       evt_pairing, obj_meth1, obj_meth2;
        int             bytes_read;
        int             raw_etype;
        int             idx;

        // Return all the Topology names.
        if ( num_topology_returned < 3 )
            return Kind.TOPOLOGY;

        // Return the Arrow Category which is non existed in CLOG file.
        if ( isFirstPeekForCategory )
            return Kind.CATEGORY;

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

                        obj_meth1 = ( ObjMethod ) evtdefs.get( def.startetype );
                        obj_meth2 = ( ObjMethod ) evtdefs.get( def.endetype );
                        if ( obj_meth1 == null || obj_meth2 == null ) {
                            stateform = new Topo_State();
                            idx  = ObjDef.getNextCategoryIndex();
                            statedef = new ObjDef( idx, def, stateform, 1 );
                            stateform.setCategory( statedef );
                            evtdefs.put( statedef.start_evt,
                                         stateform.getStartEventObjMethod() );
                            evtdefs.put( statedef.final_evt,
                                         stateform.getFinalEventObjMethod() );
                        }
                        else {  // i.e. obj_meth1 != null && obj_meth2 != null 
                            if ( obj_meth1.obj == obj_meth2.obj ) {
                                stateform = ( Topo_State ) obj_meth1.obj;
                                statedef = ( ObjDef ) stateform.getCategory();
                                statedef.setName( def.description );
                                statedef.setColor(
                                     ColorNameMap.getColorAlpha( def.color ) );
                            }
                            else {
                                System.err.println( "**** Error! "
                                                  + obj_meth1.obj + "!="
                                                  + obj_meth2.obj );
                            }
                        }

                        return Kind.CATEGORY;
                    case RecRaw.RECTYPE:
                        bytes_read = raw.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        raw_etype = raw.etype.intValue();
                        if (    raw_etype != Const.EvtType.CONSTDEF 
                             && raw_etype != Const.AllType.UNDEF ) {
                       	    evt_pairing = (ObjMethod) evtdefs.get( raw.etype );
                            // arglist = new Object[] { header, raw };
                            arglist[ 0 ] = header;
                            arglist[ 1 ] = raw;
                            drawobj = null;
                            try {
                                drawobj = (Primitive) evt_pairing.method
                                                     .invoke( evt_pairing.obj,
                                                              arglist );
                            } catch ( IllegalAccessException err ) {
                                err.printStackTrace();
                                System.err.println( "Offending RecRaw = "
                                                  + raw );
                            // catching NoMatchingEventException
                            } catch ( InvocationTargetException err ) {
                                err.printStackTrace();
                            } catch ( NullPointerException nullerr ) {
                                nullerr.printStackTrace();
                                System.err.println( "Offending RecHeader = "
                                                  + header );
                                System.err.println( "Offending RecRaw = "
                                                  + raw );
                                System.exit(1);
                            }

                            if ( drawobj != null )
                                return Kind.PRIMITIVE;
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
                        blk_ins = super.getBlockStream();
                        // System.out.println( "End Of Block" );
                        break;
                    case logformat.clog.Const.RecType.ENDLOG:
                        blk_ins = null;
                        // System.out.println( "End Of File" );
                        break;
                    default:
                        System.err.println( "Unknown Record type = "
                                          + rectype );
                }   // endof switch ( rectype )
            }   //  endof while ( rectype != (ENDBLOCK/ENDLOG) )
        }   //  endof while ( getBlockStream() )

        return Kind.EOF;
    }

    public Topology getNextTopology()
    {
        switch ( num_topology_returned ) {
            case 0:
                num_topology_returned = 1;
                return Topology.EVENT;
            case 1:
                num_topology_returned = 2;
                return Topology.STATE;
            case 2:
                num_topology_returned = 3;
                return Topology.ARROW;
            default:
                System.err.println( "All Topology Names have been returned" );
        }
        return null;
    }

    // getNextCategory() is called after peekNextKind() returns Kind.CATEGORY
    public Category getNextCategory()
    {
        if ( isFirstPeekForCategory ) {
            isFirstPeekForCategory = false;
            topos.add( arrowdef.getTopology() );
            return arrowdef;
        }
        else {
            topos.add( statedef.getTopology() );
            return statedef;
        }
    }

    // getNextPrimitive() is called after peekNextKind() returns Kind.PRIMITIVE
    public Primitive getNextPrimitive()
    {
        return drawobj;
    }

    // getNextComposite() is called after peekNextKind() returns Kind.COMPOSITE
    public Composite getNextComposite()
    {
        return null;
    }

    // getNextYCoordMap() is called after peekNextKind() returns Kind.YCOORDMAP
    public YCoordMap getNextYCoordMap()
    {
        return null;
    }

    public long getTotalBytesRead()
    {
        return total_bytesize;
    }

    public List getAllUsedTopos()
    {
        return topos;
    }

    public long getNumberOfUnMatchedEvents()
    {
        int Nunmatched = 0;
        Iterator topos_itr = topos.iterator();
        while ( topos_itr.hasNext() )
            Nunmatched += ( (TwoEventsMatching) topos_itr.next() )
                          .getPartialObjects().size();
        return Nunmatched;
    }
}

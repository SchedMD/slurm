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
import java.lang.reflect.*;
import java.io.*;

import base.drawable.*;
import logformat.clog2.*;

public class InputLog extends logformat.clog2.InputLog
                      implements base.drawable.InputAPI
{
    private int                    next_avail_kindID;
    private TopologyIterator       itr_topo;
    private ContentIterator        itr_objdef_dobj;
    private YCoordMapIterator      itr_ycoordmap;
    private boolean                isFirstPeekForCategory;

    public InputLog( String pathname )
    {
        super( pathname );
        itr_topo         = null;
        itr_ycoordmap    = null;
        itr_objdef_dobj  = null;
        this.initialize();
    }

    private void initialize()
    {
        itr_topo = new TopologyIterator( Kind.TOPOLOGY_ID );
        // Initialize boolean variable, isFirstPeekForCategory
        isFirstPeekForCategory = true;
    }

    public Kind peekNextKind()
    {
        switch ( next_avail_kindID ) {
            case Kind.TOPOLOGY_ID :
                if ( itr_topo.hasNext() )
                    return Kind.TOPOLOGY;
                itr_objdef_dobj  = new ContentIterator( Kind.PRIMITIVE_ID );
                    return Kind.CATEGORY;  /* return the Arrow Category */
            case Kind.CATEGORY_ID :
            case Kind.PRIMITIVE_ID :
            case Kind.COMPOSITE_ID :
                if ( itr_objdef_dobj.hasNext() ) {
                    switch ( next_avail_kindID ) {
                        case Kind.CATEGORY_ID:
                            return Kind.CATEGORY;
                        case Kind.PRIMITIVE_ID:
                            return Kind.PRIMITIVE;
                        case Kind.COMPOSITE_ID:
                            return Kind.COMPOSITE;
                    }
                }
                itr_ycoordmap  = new YCoordMapIterator( Kind.YCOORDMAP_ID );
            case Kind.YCOORDMAP_ID :
                if ( itr_ycoordmap.hasNext() )
                    return Kind.YCOORDMAP;
            case Kind.EOF_ID:
                return Kind.EOF;
            default:
                System.err.println( "InputLog.peekNextKind(): Error!\n"
                                  + "\tUnknown Kind ID: " + next_avail_kindID );
                break;
        }
        return null;
    }

    public Topology getNextTopology()
    {
        return (Topology) itr_topo.next();
    }

    // getNextCategory() is called after peekNextKind() returns Kind.CATEGORY
    public Category getNextCategory()
    {
        if ( isFirstPeekForCategory ) {
            isFirstPeekForCategory  = false;
            return itr_objdef_dobj.getArrowCategory();
        }
        else
            return (Category) itr_objdef_dobj.next();
    }

    // getNextYCoordMap() is called after peekNextKind() returns Kind.YCOORDMAP
    public YCoordMap getNextYCoordMap()
    {
        return (YCoordMap) itr_ycoordmap.next();
    }

    // getNextPrimitive() is called after peekNextKind() returns Kind.PRIMITIVE
    public Primitive getNextPrimitive()
    {
        return (Primitive) itr_objdef_dobj.next();
    }

    // getNextComposite() is called after peekNextKind() returns Kind.COMPOSITE
    public Composite getNextComposite()
    {
        return (Composite) itr_objdef_dobj.next();
    }

    public void close()
    {
        super.close();
    }

    public long getNumberOfUnMatchedEvents()
    {
        return itr_objdef_dobj.getNumberOfUnMatchedEvents();
    }

    public long getTotalBytesRead()
    {
        return itr_objdef_dobj.getTotalBytesRead();
    }




    private class TopologyIterator implements Iterator
    {
        private int  num_topology_returned;

        public TopologyIterator( int kindID )
        {
             InputLog.this.next_avail_kindID  = kindID;
             num_topology_returned  = 0;
        }

        public boolean hasNext()
        {
             return num_topology_returned < 3;
        }

        public Object next()
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
                    System.err.println( "All Topologies have been returned." );
            }
            return null;
        }

        public void remove() {}
    }


    private class YCoordMapIterator implements Iterator
    {
        private Iterator      ycoordmap_itr;
        private YCoordMap     next_ycoordmap;

        public YCoordMapIterator( int kindID )
        {
            InputLog.this.next_avail_kindID  = kindID;

            List  ycoordmaps;
            ycoordmaps     = InputLog.this.itr_objdef_dobj.getYCoordMapList();
            ycoordmap_itr  = ycoordmaps.iterator();
            if ( ycoordmap_itr.hasNext() )
                next_ycoordmap = (YCoordMap) ycoordmap_itr.next();
            else
                next_ycoordmap = null;
        }

        public boolean hasNext()
        {
            return  next_ycoordmap != null;
        }

        public Object next()
        {
            YCoordMap  loosen_ycoordmap  = next_ycoordmap;
            if ( ycoordmap_itr.hasNext() )
                next_ycoordmap = (YCoordMap) ycoordmap_itr.next();
            else
                next_ycoordmap = null;
            return loosen_ycoordmap;
        }

        public void remove() {}
    }



private class ContentIterator implements Iterator
{
    private MixedDataInputStream   blk_ins;
    private long                   total_bytesize;

    private CommLineIDMap          commlineIDmap;
    private Map                    evtdefs;
    private List                   topos;
    private ObjDef                 statedef;
    private ObjDef                 arrowdef;
    private ObjDef                 eventdef;
    private ObjDef                 dobjdef;
    private Primitive              drawobj;

    private RecHeader              header  ;
    private RecDefState            staterec;
    private RecDefEvent            eventrec;
    private RecDefConst            constrec;
    private RecBare                bare    ;
    private RecCargo               cargo   ;
    private RecMsg                 msg     ;
    private RecColl                coll    ;
    private RecComm                comm    ;
    private RecSrc                 src     ;
    private RecTshift              tshift  ;

    private Topo_Event             eventform;
    private Topo_Arrow             arrowform;
    private Topo_State             stateform;
    private ObjMethod              obj_fn;
    private Object[]               arglist;

    public ContentIterator( int kindID )
    {
        // Map to hold (CommLineID's lineID, CommLineID) pairs. 
        commlineIDmap  = new CommLineIDMap();
        commlineIDmap.initialize();

        InputLog.this.next_avail_kindID  = kindID;

        // Stack event matching object function list, evtdefs.
        evtdefs   = new HashMap();

        // drawable's topology list of objdefs, ie.statedef, arrowdef & eventdef
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
        arrowdef.setColor( new ColorAlpha( arrowdef.getColor(),
                                           ColorAlpha.OPAQUE ) );
        arrowform.setCategory( arrowdef );
        evtdefs.put( arrowdef.start_evt,
                     arrowform.getStartEventObjMethod() );
        evtdefs.put( arrowdef.final_evt,
                     arrowform.getFinalEventObjMethod() );

        // Gather all the known (i.e. MPI's, internal MPE/CLOG's) and
        // user-defined undefined RecDefStates, i.e. CLOG_Rec_StateDef_t.
        List defs = InputLog.super.getKnownUndefinedInitedStateDefs();
        defs.addAll( InputLog.super.getUserUndefinedInitedStateDefs() );

        // Convert them to the appropriate categories + corresponding
        // stack event matching object functions.
        Iterator itr = defs.iterator();
        while ( itr.hasNext() ) {
            staterec = ( RecDefState ) itr.next();

            stateform = new Topo_State();
            def_idx   = ObjDef.getNextCategoryIndex();
            statedef  = new ObjDef( def_idx, staterec, stateform, 1 );
            stateform.setCategory( statedef );
            evtdefs.put( statedef.start_evt,
                         stateform.getStartEventObjMethod() );
            evtdefs.put( statedef.final_evt,
                         stateform.getFinalEventObjMethod() );
        }

        // Add all the user-defined undefined RecDefEvents, CLOG_Rec_EventDef_t.
        defs.addAll( InputLog.super.getUserUndefinedInitedEventDefs() );

        /*
        System.err.println( "\n\t evtdefs : " );
        Iterator evtdefs_itr = evtdefs.entrySet().iterator();
        while ( evtdefs_itr.hasNext() )
            System.err.println( evtdefs_itr.next() );
        */

        // Create various CLOG records as place holders for CLOG parser.
        header    = new RecHeader();
        staterec  = new RecDefState();
        eventrec  = new RecDefEvent();
        constrec  = new RecDefConst();
        bare      = new RecBare();
        cargo     = new RecCargo();
        msg       = new RecMsg();
        coll      = new RecColl();
        comm      = new RecComm();
        src       = new RecSrc();
        tshift    = new RecTshift();

        // Initialize argument list variable
        arglist   = new Object[ 2 ];

        // Initialize the total_bytesize read from the CLOG stream
        total_bytesize = 0;

        // Initialize the CLOG block-input-stream for peekNextKind()
        blk_ins = InputLog.super.getBlockStream();
    }

    public boolean hasNext()
    {
        ObjMethod       evt_pairing, obj_meth1, obj_meth2;
        CommLineID      commlineID;
        int             bytes_read;
        int             bare_etype, cargo_etype, msg_etype;
        int             rectype;
        int             idx;

        while ( blk_ins != null ) {
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

                        obj_meth1 = ( ObjMethod )
                                    evtdefs.get( staterec.startetype );
                        obj_meth2 = ( ObjMethod )
                                    evtdefs.get( staterec.finaletype );
                        if ( obj_meth1 == null || obj_meth2 == null ) {
                            stateform = new Topo_State();
                            idx  = ObjDef.getNextCategoryIndex();
                            statedef = new ObjDef( idx, staterec,
                                                   stateform, 1 );
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
                                statedef.setName( staterec.name );
                                statedef.setColor(
                                 ColorNameMap.getColorAlpha( staterec.color ) );                                statedef.setInfoKeys( staterec.format );
                            }
                            else {
                                System.err.println( "**** Error! "
                                                  + obj_meth1.obj + "!="
                                                  + obj_meth2.obj );
                            }
                        }
                        dobjdef  = statedef;
                        InputLog.this.next_avail_kindID = Kind.CATEGORY_ID;
                        return true;
                    case RecDefEvent.RECTYPE:
                        bytes_read = eventrec.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        obj_meth1 = ( ObjMethod ) evtdefs.get( eventrec.etype );
                        if ( obj_meth1 == null ) {
                            eventform = new Topo_Event();
                            idx  = ObjDef.getNextCategoryIndex();
                            eventdef = new ObjDef( idx, eventrec,
                                                   eventform, 1 );
                            eventform.setCategory( eventdef );
                            evtdefs.put( eventdef.start_evt,
                                         eventform.getEventObjMethod() );
                        }
                        else { // i.e. obj_meth1 != null
                            eventform = ( Topo_Event ) obj_meth1.obj;
                            eventdef = ( ObjDef ) eventform.getCategory();
                            eventdef.setName( eventrec.name );
                            eventdef.setColor(
                              ColorNameMap.getColorAlpha( eventrec.color ) );
                            eventdef.setInfoKeys( eventrec.format );
                        }
                        dobjdef  = eventdef;
                        InputLog.this.next_avail_kindID = Kind.CATEGORY_ID;
                        return true;
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
                            evt_pairing = (ObjMethod) evtdefs.get( bare.etype );                            // arglist = new Object[] { header, bare=null };
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

                            if ( drawobj != null ) {
                                commlineIDmap.setCommLineIDUsed( drawobj );
                                InputLog.this.next_avail_kindID
                                = Kind.PRIMITIVE_ID;
                                return true;
                            }
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

                            if ( drawobj != null ) {
                                commlineIDmap.setCommLineIDUsed( drawobj );
                                InputLog.this.next_avail_kindID
                                = Kind.PRIMITIVE_ID;
                                return true;
                            }
                        }
                        break;
                    case RecMsg.RECTYPE:
                        bytes_read = msg.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;

                        msg_etype = msg.etype.intValue();
                        if ( msg_etype != Const.AllType.UNDEF ) {
                            evt_pairing = (ObjMethod) evtdefs.get( msg.etype );
                            // arglist = new Object[] { header, msg };
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

                            if ( drawobj != null ) {
                                commlineIDmap.setCommLineIDUsed( drawobj );
                                InputLog.this.next_avail_kindID
                                = Kind.PRIMITIVE_ID;
                                return true;
                            }
                        }
                        break;
                    case RecColl.RECTYPE:
                        bytes_read = coll.skipBytesFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        break;
                    case RecComm.RECTYPE:
                        bytes_read = comm.readFromDataStream( blk_ins );
                        total_bytesize += bytes_read;
                        commlineID = new CommLineID( comm );
                        commlineIDmap.addCommLineID( commlineID );
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
                        blk_ins = InputLog.super.getBlockStream();
                        // System.out.println( "End Of Block" );
                        break;
                    case logformat.clog2.Const.RecType.ENDLOG:
                        blk_ins = null;
                        // System.out.println( "End Of File" );
                        break;
                    default:
                        System.err.println( "Unknown Record type = "
                                          + rectype );
                }   // endof switch ( rectype )
            }   //  endof while ( rectype != (ENDBLOCK/ENDLOG) )
        }   //  endof while ( getBlockStream() )

        return false;
    }

    public Object next()
    {
        switch (InputLog.this.next_avail_kindID) {
            case Kind.CATEGORY_ID:
                topos.add( dobjdef.getTopology() );
                return dobjdef;
            case Kind.PRIMITIVE_ID:
                return drawobj;
            default:
                return null;
        }
    }

    public void remove() {}

    public Category getArrowCategory()
    {
        topos.add( arrowdef.getTopology() );
        return arrowdef;
    }

    public List getYCoordMapList()
    {
        commlineIDmap.finish();
        return commlineIDmap.createYCoordMapList();
    }

    public long getTotalBytesRead()
    {
        return total_bytesize;
    }

    public long getNumberOfUnMatchedEvents()
    {
        Topology  topo;
        int num_matched = 0;
        Iterator topos_itr = topos.iterator();
        while ( topos_itr.hasNext() ) {
            topo = ( Topology ) topos_itr.next();
            if ( topo instanceof TwoEventsMatching ) {
                num_matched += ( (TwoEventsMatching) topo )
                               .getPartialObjects().size();
            }
        }
        return num_matched;
    }
}


}   // End of InputLog class

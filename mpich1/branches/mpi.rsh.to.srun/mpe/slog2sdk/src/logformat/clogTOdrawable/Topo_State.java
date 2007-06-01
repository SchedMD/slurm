/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clogTOdrawable;

import java.util.List;
import java.util.LinkedList;
import java.util.Iterator;
import java.lang.reflect.*;

import logformat.clog.RecHeader;
import logformat.clog.RecRaw;
import base.drawable.Coord;
import base.drawable.Category;
import base.drawable.Topology;
import base.drawable.Primitive;

public class Topo_State extends Topology
                        implements TwoEventsMatching 
{
    private static   Class[]   argtypes      = null;
    private          List      partialstates = null;

    private          Category  type          = null;

    public Topo_State()
    {
        super( Topology.STATE_ID );
        if ( argtypes == null )
            argtypes = new Class[] { RecHeader.class, RecRaw.class };
        partialstates = new LinkedList();
    }

/*
    public Topo_State( String in_name )
    {
        super( in_name );
        if ( argtypes == null )
            argtypes = new Class[] { RecHeader.class, RecRaw.class };
        partialstates = new LinkedList();
    }
*/

    public void setCategory( final Category in_type )
    {
        type = in_type;
    }

    // only for extended class
    public Category getCategory()
    {
        return type;
    }

    // The 2nd argument, raw, is NOT used.  
    // It is there to fulfill the interface requirement
    public Primitive matchStartEvent( final RecHeader header,
                                      final RecRaw    raw )
    throws NoMatchingEventException
    {
        Obj_State state = new Obj_State( this.getCategory() );
        state.setStartVertex( new Coord( header.timestamp, header.taskID ) );
        partialstates.add( state );
        return null;
    }

    // The 2nd argument, raw, is NOT used here.  
    // It is there to fulfill the interface, match_events.
    public Primitive matchFinalEvent( final RecHeader header,
                                      final RecRaw    raw )
    throws NoMatchingEventException
    {
        Obj_State  state;

        Iterator itr = partialstates.iterator();
        while ( itr.hasNext() ) {
            state = ( Obj_State ) itr.next();
            if ( state.getStartVertex().lineID == header.taskID ) {
                itr.remove();
                state.setFinalVertex( new Coord( header.timestamp,
                                                 header.taskID ) );
                return state;
            }
        }
        throw new NoMatchingEventException( "No matching State end-event "
                                          + "for Record " + header
                                          + ", " + raw );
    }

    public ObjMethod getStartEventObjMethod()
    {
        ObjMethod obj_fn = new ObjMethod();
        obj_fn.obj = this;
        try {
            obj_fn.method = this.getClass().getMethod( "matchStartEvent",
                                                       argtypes );
        } catch ( NoSuchMethodException err ) {
            err.printStackTrace();
            return null;
        }
        return obj_fn;
    }

    public ObjMethod getFinalEventObjMethod()
    {
        ObjMethod obj_fn = new ObjMethod();
        obj_fn.obj = this;
        try {
            obj_fn.method = this.getClass().getMethod( "matchFinalEvent",
                                                       argtypes );
        } catch ( NoSuchMethodException err ) {
            err.printStackTrace();
            return null;
        }
        return obj_fn;
    }


    public List getPartialObjects()
    {
        return partialstates;
    }
}

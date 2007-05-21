/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import java.util.List;
import java.util.LinkedList;
import java.util.Iterator;
import java.lang.reflect.*;

import logformat.clog2.RecHeader;
import logformat.clog2.RecCargo;
import base.drawable.Coord;
import base.drawable.Category;
import base.drawable.Topology;
import base.drawable.Primitive;

public class Topo_State extends Topology
                        implements TwoEventsMatching 
{
    private static   Class[]   argtypes            = null;
    private          List      partialstates       = null;

    private          Category  type                = null;

    public Topo_State()
    {
        super( Topology.STATE_ID );
        if ( argtypes == null )
            argtypes = new Class[] { RecHeader.class, RecCargo.class };
            // argtypes = new Class[] { RecHeader.class, RecBare.class };
        partialstates = new LinkedList();
    }

/*
    public Topo_State( String in_name )
    {
        super( in_name );
        if ( argtypes == null )
            argtypes = new Class[] { RecHeader.class, RecBare.class };
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

    // The 2nd argument, cargo, is expected to be NULL;
    public Primitive matchStartEvent( final RecHeader header,
                                      final RecCargo  cargo )
    throws NoMatchingEventException
    {
        Obj_State state = new Obj_State( this.getCategory() );
        state.setStartVertex( new Coord( header.time, header.lineID ) );
        if ( cargo != null ) // This is a precaution measure
            state.setInfoBuffer( cargo.bytes );
        partialstates.add( state );
        return null;
    }

    // If 2nd argument is of class RecCargo, then it will be used here.  
    public Primitive matchFinalEvent( final RecHeader header,
                                      final RecCargo  cargo )
    throws NoMatchingEventException
    {
        Obj_State  state;

        Iterator itr = partialstates.iterator();
        while ( itr.hasNext() ) {
            state = ( Obj_State ) itr.next();
            if ( state.getStartVertex().lineID == header.lineID ) {
                itr.remove();
                state.setFinalVertex( new Coord( header.time, header.lineID ) );
                if ( cargo != null )
                    state.setInfoBuffer( cargo.bytes );
                    // ? need new copy of byte[] bytes for setInfoBuffer(), GC ?
                return state;
            }
        }
        throw new NoMatchingEventException( "No matching State end-event "
                                          + "for Record " + header
                                          + ", " + cargo );
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

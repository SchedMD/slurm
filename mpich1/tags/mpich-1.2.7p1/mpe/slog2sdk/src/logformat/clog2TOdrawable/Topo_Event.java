/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import logformat.clog2.RecHeader;
import logformat.clog2.RecCargo;
import base.drawable.Coord;
import base.drawable.Category;
import base.drawable.Topology;
import base.drawable.Primitive;

public class Topo_Event extends Topology
{
    private static   Class[]   argtypes            = null;

    private          Category  type                = null;

    public Topo_Event()
    {
        super( Topology.EVENT_ID );
        if ( argtypes == null )
            argtypes = new Class[] { RecHeader.class, RecCargo.class };
            // argtypes = new Class[] { RecHeader.class, RecBare.class };
    }

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
    public Primitive matchEvent( final RecHeader header,
                                 final RecCargo  cargo )
    throws NoMatchingEventException
    {
        Obj_Event event = new Obj_Event( this.getCategory() );
        event.setStartVertex( new Coord( header.time, header.lineID ) );
        if ( cargo != null ) // This is a precaution measure
            event.setInfoBuffer( cargo.bytes );
        return event;
    }

    public ObjMethod getEventObjMethod()
    {
        ObjMethod obj_fn = new ObjMethod();
        obj_fn.obj = this;
        try {
            obj_fn.method = this.getClass().getMethod( "matchEvent",
                                                       argtypes );
        } catch ( NoSuchMethodException err ) {
            err.printStackTrace();
            return null;
        }
        return obj_fn;
    }
}

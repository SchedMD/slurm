/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import base.drawable.Coord;
import base.drawable.Category;
import base.drawable.Primitive;

public class Obj_Event extends Primitive
{
    public Obj_Event()
    {
        super( 1 );
    }

    public Obj_Event( final Category obj_type )
    {
        super( obj_type, 1 );
    }

    public Obj_Event( final Category obj_type, final Coord vtx )
    {
        super( obj_type, 1 );
        super.setStartVertex( vtx );
    }

    public String toString()
    {
        return ( "Event{ " + super.toString() + " }" );
    }
}

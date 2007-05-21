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

public class Obj_State extends Primitive
{
    public Obj_State()
    {
        super( 2 );
    }

    public Obj_State( final Category obj_type )
    {
        super( obj_type, 2 );
    }

    public Obj_State( final Category obj_type,
                      final Coord  start_vtx, final Coord  final_vtx )
    {
        super( obj_type, 2 );
        super.setStartVertex( start_vtx );
        super.setFinalVertex( final_vtx );
    }

    public String toString()
    {
        return ( "State{ " + super.toString() + " }" );
    }
}

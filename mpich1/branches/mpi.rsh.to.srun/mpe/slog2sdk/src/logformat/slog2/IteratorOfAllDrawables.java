/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Iterator;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;


/*
   Iterators to return Drawables of all type (Shadow/Primitive/Composite)
   in the given Drawable.Order.
*/
public class IteratorOfAllDrawables implements Iterator
{
    // Drawable.INCRE_STARTTIME_ORDER defines the drawing order of drawables
    // (especially for State), i.e.first Increasing Starttime and
    // then Decreasing EndTime.
    // Drawable.INCRE_FINALTIME_ORDER define the order of drawables to
    // be passed through TRACE-API., i.e. first Increasing Endtime and
    // then Increaing Starttime.

    private Iterator         nestable_itr;
    private Iterator         nestless_itr;
    private Drawable.Order   dobj_order;
    private Drawable         nestable_dobj;
    private Drawable         nestless_dobj;
    private Drawable         next_drawable;

    // The 2 input Iterators are assumed to in the same order as the
    // specified Drawable.Order.
    public IteratorOfAllDrawables( final Iterator       in_nestable_itr,
                                   final Iterator       in_nestless_itr,
                                   final Drawable.Order in_dobj_order )
    {
        nestable_itr   = in_nestable_itr;
        nestless_itr   = in_nestless_itr;
        dobj_order     = in_dobj_order;
        nestable_dobj  = null;
        nestless_dobj  = null;
        next_drawable  = this.getNextInQueue();
    }

    private Drawable getNextInQueue()
    {
        Drawable   next_dobj;

        if ( nestable_dobj == null ) {
            if ( nestable_itr.hasNext() )
                nestable_dobj = (Drawable) nestable_itr.next();
        }
        if ( nestless_dobj == null ) {
            if ( nestless_itr.hasNext() )
                nestless_dobj = (Drawable) nestless_itr.next();
        }

        if ( nestable_dobj != null && nestless_dobj != null ) {
            if ( dobj_order.compare( nestable_dobj, nestless_dobj ) <= 0 ) {
                next_dobj = nestable_dobj;
                nestable_dobj = null;
                return next_dobj;
            }
            else {
                next_dobj = nestless_dobj;
                nestless_dobj = null;
                return next_dobj;
            }
        }

        if ( nestable_dobj != null ) {
            next_dobj = nestable_dobj;
            nestable_dobj = null;
            return next_dobj;
        }

        if ( nestless_dobj != null ) {
            next_dobj = nestless_dobj;
            nestless_dobj = null;
            return next_dobj;
        }

        return null;
    }

    public boolean hasNext()
    {
        return next_drawable != null;
    }

    public Object next()
    {
        Drawable  returning_dobj;

        returning_dobj = next_drawable;
        next_drawable  = this.getNextInQueue();
        return returning_dobj;
    }

    public void remove() {}
}

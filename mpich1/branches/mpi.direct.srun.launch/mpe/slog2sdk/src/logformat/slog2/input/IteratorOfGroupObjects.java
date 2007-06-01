/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.input;

import java.util.Iterator;

import base.drawable.TimeBoundingBox;

/*
   This iterator iterates over a set of consecutive sub-iterators one at a time.
   After one of sub-iterators has been exhaustively iterated, the next one
   in the set will be iterated.  The order of sub-iterator being iterated is
   defined by setObjGrpItr().
*/
public abstract class IteratorOfGroupObjects implements Iterator
{
    private  TimeBoundingBox    timeframe;
    private  Iterator           objects_itr;

    public IteratorOfGroupObjects( final TimeBoundingBox  tframe )
    {
        timeframe          = tframe;
    }

    // Initialize the Iterator of obj-groups( [] / Collection )
    // __before__ any invocation of nextObjGrpItr()
    protected void setObjGrpItr( Iterator  init_itr )
    {
        objects_itr        = init_itr;
    }

    // return NULL when no more obj-group in obj-groups( [] / Collection )
    protected abstract Iterator nextObjGrpItr( final TimeBoundingBox tframe );

    // When current objects_itr becomes empty, update it with nextObjGrpItr()
    // until nextObjGrpItr() returns null.
    public boolean hasNext()
    {
        if ( objects_itr != null ) {
            do {
                if ( objects_itr.hasNext() )
                    return true;
            } while (    ( objects_itr = this.nextObjGrpItr( timeframe ) )
                      != null );
        }
        return false;
    }

    public Object next()
    {
        return objects_itr.next();
    }

    public void remove() {}
}

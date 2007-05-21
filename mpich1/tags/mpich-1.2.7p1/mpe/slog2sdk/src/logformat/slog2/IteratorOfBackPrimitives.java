/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.List;
import java.util.TreeSet;
import java.util.Iterator;
import java.util.ListIterator;
import java.util.NoSuchElementException;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import base.drawable.Composite;
import base.drawable.Primitive;

/*
   Iterator of Primitives in a given List in Decreasing StartTime order.
   The primitive returned by next() overlaps with the timeframe specified.
 */
public class IteratorOfBackPrimitives implements Iterator
{
    // Drawable.INCRE_STARTTIME_ORDER defines the drawing order of drawables
    // (especially for State), i.e.first Increasing Starttime and
    // then Decreasing EndTime.

    private ListIterator     drawables_itr;
    private TimeBoundingBox  timeframe;

    private TreeSet          set_primes;
    private Primitive        next_primitive;

    public IteratorOfBackPrimitives(       List             dobjs_list,
                                     final TimeBoundingBox  tframe )
    {
        drawables_itr  = dobjs_list.listIterator( dobjs_list.size() );
        timeframe      = tframe;
        set_primes     = new TreeSet( Drawable.INCRE_STARTTIME_ORDER );
        next_primitive = this.getNextInQueue();
    }

    private Primitive getNextInQueue()
    {
        Drawable   itr_dobj;
        Composite  itr_cmplx;
        Primitive  next_prime;

        next_prime = null;
        while ( drawables_itr.hasPrevious() ) {
            itr_dobj = (Drawable) drawables_itr.previous();
            if ( itr_dobj.overlaps( timeframe ) ) {
                if ( itr_dobj instanceof Composite ) {
                    itr_cmplx  = (Composite) itr_dobj;
                    itr_cmplx.addPrimitivesToSet( set_primes, timeframe );
                    try {
                        next_prime = (Primitive) set_primes.last();
                        set_primes.remove( next_prime );
                        return next_prime;
                    } catch ( NoSuchElementException err ) {}
                }
                else { // if ( itr_dobj instanceof Primitive )
                    if ( ! set_primes.isEmpty() ) {
                        set_primes.add( itr_dobj );
                        next_prime = (Primitive) set_primes.last();
                        set_primes.remove( next_prime );
                        return next_prime;
                    }
                    else {
                        next_prime = (Primitive) itr_dobj;
                        return next_prime;
                    }
                }
            }
        }

        if ( next_prime == null && !set_primes.isEmpty() ) {
            next_prime = (Primitive) set_primes.last();
            set_primes.remove( next_prime );
            return next_prime;
        }

        return null;
    }

    public boolean hasNext()
    {
        return next_primitive != null;
    }

    public Object next()
    {
        Primitive  returning_prime;

        returning_prime = next_primitive;
        next_primitive  = this.getNextInQueue();
        return returning_prime;
    }

    public void remove() {}
}   // private class IteratorOfBackPrimitives;

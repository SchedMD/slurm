/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.EventListener;


/**
 * Defines an object which listens for TimeEvents.
 *
 * @author Anthony Chan
 */
public interface TimeListener extends EventListener {
    /**
     * Invoked when the target of the listener has changed its state, i.e. time.
     *
     * @param e  a TimeEvent object
     */
    public void timeChanged( TimeEvent evt );
}

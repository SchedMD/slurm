/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.EventObject;


/**
 * TimeEvent is used to notify interested parties that 
 * time has changed in the Time Model.
 *
 * @author Anthony Chan
 */
public class TimeEvent extends EventObject {
    /**
     * Constructs a TimeEvent object.
     *
     * @param source  the Object that is the source of the TimeEvent
     *                (typically <code>this</code>)
     */
    public TimeEvent( Object source )
    {
        super( source );
    }
}

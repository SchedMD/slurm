/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

public interface WaitingContainer
{
    // execute this code when a long running job is being dispatched
    public void initializeWaiting();

    // execute this code when the long running job is done
    public void finalizeWaiting();
}

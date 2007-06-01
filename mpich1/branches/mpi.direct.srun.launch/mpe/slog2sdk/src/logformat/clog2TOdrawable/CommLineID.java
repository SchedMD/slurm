/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import logformat.clog2.RecComm;

public class CommLineID
{
    public         int       lineID;  // lineID used in drawable
    public         int       icomm;   // created comm's ID
    public         int       rank;    // icomm rank of the process
    public         int       wrank;   // MPI_COMM_WORLD rank of the process
    public         int       etype;   // type of communicator creation
    private        boolean   isUsed;

    public CommLineID( RecComm comm_rec )
    {
        lineID  = comm_rec.lineID;
        icomm   = comm_rec.icomm;
        rank    = comm_rec.rank;
        wrank   = comm_rec.wrank;
        etype   = comm_rec.etype.intValue();
        isUsed  = false;
    }

    public void setUsed( boolean flag )
    {
        isUsed  = flag;
    }

    public boolean isUsed()
    {
        return isUsed;
    }
}

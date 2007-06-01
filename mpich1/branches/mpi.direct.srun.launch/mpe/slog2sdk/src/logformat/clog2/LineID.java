/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;
import java.util.*;


public class LineID
{
    private static int CommWorldSize = 0;

    public static void setCommRank2LineIDxForm( int comm_world_size )
    {
        CommWorldSize  = comm_world_size;
    }

    /*
       The lineIDs generated with this formula are distinguishable from
       each other as long as CommWorldSize is max number of ranks within
       MPI_COMM_WORLD, true for MPI-1.
    */
    public static int compute( int icomm, int rank )
    {
        return icomm * CommWorldSize + rank;
    }
}

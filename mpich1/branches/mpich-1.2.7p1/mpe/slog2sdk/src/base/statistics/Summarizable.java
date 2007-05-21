/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import base.drawable.Topology;

public class Summarizable
{
    private Topology    topo;
    private int         rowID_start;
    private int         rowID_final;
    private Object      clicked_obj;

    public Summarizable( Object the_clicked_obj, Topology the_topo,
                         int    the_rowID_start, int      the_rowID_final )
    {
        topo         = the_topo;
        rowID_start  = the_rowID_start;
        rowID_final  = the_rowID_final;
        clicked_obj  = the_clicked_obj;
    }

    public Topology  getTopology()
    { return topo; }

    public int  getStartRowID()
    { return rowID_start; }

    public int  getFinalRowID()
    { return rowID_final; }

    public Object  getClickedObject()
    { return clicked_obj; }
}

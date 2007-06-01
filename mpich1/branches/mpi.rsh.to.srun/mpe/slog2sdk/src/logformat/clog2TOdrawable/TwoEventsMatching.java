/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import java.util.List;

public interface TwoEventsMatching
{
    public ObjMethod getStartEventObjMethod();

    public ObjMethod getFinalEventObjMethod();

    public List getPartialObjects();
}

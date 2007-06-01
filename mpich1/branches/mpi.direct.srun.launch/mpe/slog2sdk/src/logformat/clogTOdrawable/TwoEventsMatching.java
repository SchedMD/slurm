/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clogTOdrawable;

import java.util.List;

import base.drawable.Primitive;
import logformat.clog.RecHeader;
import logformat.clog.RecRaw;

public interface TwoEventsMatching
{
    public Primitive matchStartEvent( final RecHeader header,
                                      final RecRaw    raw )
    throws NoMatchingEventException;

    public Primitive matchFinalEvent( final RecHeader header,
                                      final RecRaw    raw )
    throws NoMatchingEventException;

    public ObjMethod getStartEventObjMethod();

    public ObjMethod getFinalEventObjMethod();

    public List getPartialObjects();
}

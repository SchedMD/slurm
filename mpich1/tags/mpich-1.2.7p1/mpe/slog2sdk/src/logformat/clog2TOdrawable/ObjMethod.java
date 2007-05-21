/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import java.lang.reflect.*;

class ObjMethod
{
    public Object obj;
    public Method method;

    public String toString()
    {
        return( obj.toString() + ": " + method.toString() );
    }
}

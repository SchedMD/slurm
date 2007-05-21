/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

public class Alias
{
    private Object  obj;
    private String  rep;

    public Alias( final Object new_obj, final String new_rep )
    {
        obj = new_obj;
        rep = new_rep;
    }

    public Object toValue()
    {
        return obj;
    }

    public String toString()
    {
        return rep;
    }
}

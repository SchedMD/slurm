/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.io.*;
import java.util.*;

public class named_vector extends Vector
{
    private String name;

    public named_vector( String in_name )
    {
        super();
        name = in_name;
    }

    public boolean isNameEqualTo( String a_name )
    {
        return name.equals( a_name );
    }

    public boolean isNameEqualTo( named_vector a_named_vtr )
    {
        return name.equals( a_named_vtr.name );
    }

    public String toString()
    {
        if ( super.size() > 0 )
            return " " + name + "  has " + super.size() + " leafnodes. "
                 + super.toString();
        else
            return " " + name + " ";
    }
}

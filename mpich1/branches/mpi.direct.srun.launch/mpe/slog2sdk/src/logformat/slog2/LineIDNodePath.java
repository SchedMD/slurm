/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package slog2;

import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.io.DataInput;
import java.io.DataOutput;

public class LineIDNodePath extends ArrayList
{
    public LineIDNodePath()
    {
        super();
    }

    public LineIDNodePath( Integer lineID )
    {
        super();
        super.add( lineID );
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
    }

}

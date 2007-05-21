/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.DataInput;
import java.io.DataOutput;

public interface DataIO
{
    public void writeObject( DataOutput outs ) throws java.io.IOException;

    public void readObject( DataInput ins ) throws java.io.IOException;
}

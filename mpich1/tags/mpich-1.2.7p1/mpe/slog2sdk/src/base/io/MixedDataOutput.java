/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.DataOutput;

public interface MixedDataOutput extends DataOutput
{
    public void writeString( String str )
    throws java.io.IOException;
}

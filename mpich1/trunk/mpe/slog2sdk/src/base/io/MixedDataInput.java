/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

import java.io.DataInput;

public interface MixedDataInput extends DataInput
{
    public String readString()
    throws java.io.IOException;

    public String readStringWithLimit( short max_sz )
    throws java.io.IOException;
}

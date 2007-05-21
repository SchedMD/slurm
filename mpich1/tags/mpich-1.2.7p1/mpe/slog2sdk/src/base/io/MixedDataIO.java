/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package base.io;

public interface MixedDataIO // extends DataIO
{
    public void writeObject( MixedDataOutput outs ) throws java.io.IOException;

    public void readObject( MixedDataInput ins ) throws java.io.IOException;
}

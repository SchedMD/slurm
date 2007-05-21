/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

public interface InputAPI
{
    // open() is replaced by the constructor of API class.
    // public DataInputStream open( String filename );

    public Kind       peekNextKind();

    public Topology   getNextTopology();

    public Category   getNextCategory();

    public Primitive  getNextPrimitive();

    public Composite  getNextComposite();

    public YCoordMap  getNextYCoordMap();

    // close() is implemented in the subclass of API class.
    // public void close();
}

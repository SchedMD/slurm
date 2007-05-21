/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.io.DataInput;
import java.io.DataOutput;

import base.io.DataIO;

//  A place holder for "Method".  It is meant to be extended
public class Method implements DataIO
{
    public static final  int       CONNECT_COMPOSITE_STATE_ID  = 1;

    public static final  Method    CONNECT_COMPOSITE_STATE    
                                   = new Method( CONNECT_COMPOSITE_STATE_ID );

    private int        index;

    public Method( int in_index )
    {
        index  = in_index;
    }

    public int getMethodIndex()
    {
        return index;
    }

    public boolean equals( final Method a_method )
    {
        return this.index == a_method.index;
    }

    public boolean equals( Object obj )
    {
        return this.equals( (Method) obj );
    }

    // Override hashCode, so HashMap will consider 2 instances of Topology
    // the same as long as index are the same.
    // This is necessary for Topo_State
    public int hashCode()
    {
        return index;
    }

    public boolean isConnectCompositeState()
    {
        return index == this.CONNECT_COMPOSITE_STATE_ID;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeInt( this.index );
    }

    public Method( DataInput ins )
    throws java.io.IOException
    {
        index  = ins.readInt();
    } 

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        index  = ins.readInt();
    }

    public String toString()
    {
        switch ( index ) {
            case CONNECT_COMPOSITE_STATE_ID :
                return "CONNECT_COMPOSITE_STATE";
            default :
                return "Unknown Method";
        }
    }
}

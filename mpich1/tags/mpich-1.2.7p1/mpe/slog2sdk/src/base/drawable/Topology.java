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

//  A place holder for "shape".  It is meant to be extended
public class Topology implements DataIO
{
    public static final  int         EVENT_ID  = 0;
    public static final  int         STATE_ID  = 1;
    public static final  int         ARROW_ID  = 2;

    public static final  Topology    EVENT     = new Topology( EVENT_ID );
    public static final  Topology    STATE     = new Topology( STATE_ID );
    public static final  Topology    ARROW     = new Topology( ARROW_ID );

    private int        index;

    public Topology( int in_index )
    {
        index  = in_index;
    }

    public boolean equals( final Topology aTopo )
    {
        return this.index == aTopo.index;
    }

    public boolean equals( Object obj )
    {
        return this.equals( (Topology) obj );
    }

    // Override hashCode, so HashMap will consider 2 instances of Topology
    // the same as long as index are the same.
    // This is necessary for Topo_State
    public int hashCode()
    {
        return index;
    }

    public boolean isEvent()
    {
        return index == this.EVENT_ID;
    }

    public boolean isState()
    {
        return index == this.STATE_ID;
    }

    public boolean isArrow()
    {
        return index == this.ARROW_ID;
    }

    public boolean isPrimitive()
    {
        return    index == this.STATE_ID
               || index == this.ARROW_ID
               || index == this.EVENT_ID;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeInt( this.index );
    }

    public Topology( DataInput ins )
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
            case EVENT_ID :
                return "Event";
            case STATE_ID :
                return "State";
            case ARROW_ID :
                return "Arrow";
            default :
                return "Unknown";
        }
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.ArrayList;
import java.util.Iterator;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;

public class LineIDMapList extends ArrayList
                           implements MixedDataIO
{
    public LineIDMapList()
    {
        super();
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        outs.writeInt( super.size() );
        Iterator lineIDmaps = super.iterator();
        while ( lineIDmaps.hasNext() )
            ( (LineIDMap) lineIDmaps.next() ).writeObject( outs );
    }

    public LineIDMapList( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        int Nmaps = ins.readInt();
        for ( int idx = 0; idx < Nmaps; idx++ )
            super.add( new LineIDMap( ins ) );
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "\t LineIDMapList: \n" );
        Iterator lineIDmaps = super.iterator();
        for ( int map_idx = 1; lineIDmaps.hasNext(); map_idx++ ) {
            rep.append( "LineIDMap " + map_idx + ": \n" );
            rep.append( ( (LineIDMap) lineIDmaps.next() ) + "\n" );
        }
        return rep.toString();
    }
}

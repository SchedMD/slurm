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
import base.drawable.Category;

public class CategoryList extends ArrayList
                          implements MixedDataIO
{
    public CategoryList()
    {
        super();
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        outs.writeInt( super.size() );
        Iterator types = super.iterator();
        while ( types.hasNext() )
            ( (Category) types.next() ).writeObject( outs );
    }

    public CategoryList( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        int Ntypes = ins.readInt();
        for ( int idx = 0; idx < Ntypes; idx++ )
            super.add( new Category( ins ) );
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "\t CategoryList: \n" );
        Iterator types = super.iterator();
        for ( int type_idx = 1; types.hasNext(); type_idx++ )
            rep.append( type_idx + ": " + ( (Category) types.next() ) + "\n" );
        return rep.toString();
    }
}

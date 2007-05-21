/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.TreeMap;
import java.util.HashMap;
import java.util.Iterator;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
import base.drawable.Category;

// Use HashMap when the class is tested.
// public class CategoryMap extends TreeMap
public class CategoryMap extends HashMap
                         implements MixedDataIO
{
    public CategoryMap()
    {
        super();
    }

    public void removeUnusedCategories()
    {
        Iterator   objdefs_itr;
        Category   objdef;
        objdefs_itr = super.values().iterator();
        while ( objdefs_itr.hasNext() ) {
            objdef  = (Category) objdefs_itr.next();
            if ( ! objdef.isUsed() ) 
                objdefs_itr.remove();
        }
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        outs.writeInt( super.size() );
        Iterator objdefs = super.values().iterator();
        while ( objdefs.hasNext() )
            ( (Category) objdefs.next() ).writeObject( outs );
    }

    public CategoryMap( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        Category   objdef;

        int Nentries = ins.readInt();
        for ( int ientry = 0; ientry < Nentries; ientry++ ) {
            objdef = new Category( ins );
            super.put( new Integer( objdef.getIndex() ), objdef );
        }
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "\t CategoryMap: \n" );
        Iterator objdefs = super.values().iterator();
        for ( int idx = 1; objdefs.hasNext(); idx++ )
            rep.append( idx + ": " + ( (Category) objdefs.next() ) + "\n" );
        return rep.toString();
    }
}

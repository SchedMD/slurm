/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.util.Map;
import java.util.Comparator;
import java.io.DataInput;
import java.io.DataOutput;

import base.io.DataIO;

/*
   CategoryWeight extends CategorySummary which extends CategoryRatios
*/
public class CategoryWeight extends CategorySummary
                            implements DataIO
{
    public  static final int         BYTESIZE        = CategorySummary.BYTESIZE
                                                     + 4;  // type_idx

    public  static final Comparator  INDEX_ORDER     = new IndexOrder();

    private static final int INVALID_INDEX = Integer.MIN_VALUE;

    private int        type_idx;
    private Category   type;

    private int        width;    // pixel width, for SLOG-2 Input & Jumpshot
    private int        height;   // pixel height, for SLOG-2 Input & Jumpshot

    public CategoryWeight()
    {
        super();
        type           = null;
        type_idx       = INVALID_INDEX;
        width          = 0;
        height         = 0;
    }

    // For SLOG-2 Output
    public CategoryWeight( final Category new_type,
                           float new_incl_r, float new_excl_r,
                           long new_num_real_objs )
    {
        super( new_incl_r, new_excl_r, new_num_real_objs );
        type           = new_type;
        type_idx       = type.getIndex();
    }

    // For SLOG-2 Output
    public CategoryWeight( final CategoryWeight type_wgt )
    {
        super( type_wgt );
        this.type           = type_wgt.type;
        this.type_idx       = type_wgt.type_idx;
    }

    public void setPixelWidth( int wdh )
    {
        width = wdh;
    }

    public int getPixelWidth()
    {
        return width;
    }

    public void setPixelHeight( int hgt )
    {
        height = hgt;
    }

    public int getPixelHeight()
    {
        return height;
    }

    public Category getCategory()
    {
        return type;
    }

    //  For SLOG-2 Input API, used by Shadow.resolveCategory() 
    public boolean resolveCategory( final Map categorymap )
    {
        if ( type == null ) {
            if ( type_idx != INVALID_INDEX ) {
                type = (Category) categorymap.get( new Integer( type_idx ) );
                if ( type != null )
                    return true;
            }
        }
        return false;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeInt( type_idx );
        super.writeObject( outs );
    }

    public CategoryWeight( DataInput ins )
    throws java.io.IOException
    {
        super();
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        type_idx       = ins.readInt();
        super.readObject( ins );
    }

    // For InfoPanelForDrawable
    public String toInfoBoxString( int print_status )
    {
        StringBuffer rep = new StringBuffer( "legend=" );
        if ( type != null )
            rep.append( type.getName() );
        else
            rep.append( "null:" + type_idx );
        rep.append( ", " );
        rep.append( super.toInfoBoxString( print_status ) );
        
        return rep.toString();
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "(type=" + type_idx );
        if ( type != null )
            rep.append( ":" + type.getName() );
        else
            rep.append( ", " );
        rep.append( super.toString() );
        rep.append( ")" );

        return rep.toString();
    }



    private static class IndexOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryWeight type_wgt1 = (CategoryWeight) o1;
            CategoryWeight type_wgt2 = (CategoryWeight) o2;
            return type_wgt1.type_idx - type_wgt2.type_idx;
        }
    }
}

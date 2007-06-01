/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.util.Comparator;
import java.io.DataInput;
import java.io.DataOutput;

import base.io.DataIO;

public class CategorySummary extends CategoryRatios
                             implements DataIO
{
    public  static final int         BYTESIZE        = CategoryRatios.BYTESIZE
                                                     + 8; // num_real_objs

    public  static final Comparator  COUNT_ORDER     = new CountOrder();

    private long         num_real_objs;

    public CategorySummary()
    {
        super();
        num_real_objs  = 0;
    }

    // For SLOG-2 Output
    public CategorySummary( float new_incl_r, float new_excl_r,
                            long new_num_real_objs )
    {
        super( new_incl_r, new_excl_r );
        num_real_objs  = new_num_real_objs;
    }

    // For SLOG-2 Output
    public CategorySummary( final CategorySummary type_smy )
    {
        super( type_smy );
        this.num_real_objs  = type_smy.num_real_objs;
    }

    public long getDrawableCount()
    {
        return num_real_objs;
    }

    public void addDrawableCount( long new_num_real_objs )
    {
        this.num_real_objs  += new_num_real_objs;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        super.writeObject( outs );
        outs.writeLong( num_real_objs );
    }

    public CategorySummary( DataInput ins )
    throws java.io.IOException
    {
        super();
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        super.readObject( ins );
        num_real_objs  = ins.readLong();
    }

    // For InfoPanelForDrawable
    public String toInfoBoxString( int print_status )
    {
        return super.toInfoBoxString( print_status )
             + ", count=" + num_real_objs;
    }

    public String toString()
    {
        return super.toString() + ", count=" + num_real_objs;
    }



    private static class CountOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategorySummary type_smy1 = (CategorySummary) o1;
            CategorySummary type_smy2 = (CategorySummary) o2;
            long diff = type_smy1.num_real_objs - type_smy2.num_real_objs;
            return ( diff < 0 ? -1 : ( diff == 0 ? 0 : 1 ) );
        }
    }
}

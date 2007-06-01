/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import java.util.Comparator;

import base.drawable.CategoryRatios;
import base.drawable.CategorySummary;

public class CategorySummaryF extends CategoryRatios
{
    public  static final int         BYTESIZE        = CategoryRatios.BYTESIZE
                                                     + 8; // num_real_objs

    public  static final Comparator  COUNT_ORDER     = new CountOrder();

    private double       num_real_objs;

    public CategorySummaryF()
    {
        super();
        num_real_objs  = 0.0;
    }

    // For SLOG-2 Output
    public CategorySummaryF( float new_incl_r, float new_excl_r,
                             double new_num_real_objs )
    {
        super( new_incl_r, new_excl_r );
        num_real_objs  = new_num_real_objs;
    }

    // For SLOG-2 Output
    public CategorySummaryF( final CategorySummaryF type_smyf )
    {
        super( type_smyf );
        this.num_real_objs  = type_smyf.num_real_objs;
    }

    // For Jumpshot, copy construct,  CategorySummary -> CategorySummaryF.
    public CategorySummaryF( final CategorySummary type_smy )
    {
        super( type_smy );
        this.num_real_objs  = (double) type_smy.getDrawableCount();
    }

    public double getDrawableCount()
    {
        return num_real_objs;
    }

    public void addDrawableCount( double new_num_real_objs )
    {
        this.num_real_objs  += new_num_real_objs;
    }

    // For Jumpshot's TimeAveBox, i.e. statistics window
    public void rescaleDrawableCount( double ftr )
    {
        this.num_real_objs  *= ftr;
    }

    // For InfoPanelForDrawable
    public String toInfoBoxString( int print_status )
    {
        return super.toInfoBoxString( print_status )
             + ", count=" + (float) num_real_objs;
    }

    public String toString()
    {
        return super.toString() + ", count=" + (float) num_real_objs;
    }



    private static class CountOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategorySummaryF type_smyf1 = (CategorySummaryF) o1;
            CategorySummaryF type_smyf2 = (CategorySummaryF) o2;
            double diff = type_smyf1.num_real_objs - type_smyf2.num_real_objs;
            return ( diff < 0.0 ? -1 : ( diff == 0.0 ? 0 : 1 ) );
        }
    }
}

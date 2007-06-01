/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import java.util.Comparator;

import base.drawable.Category;
import base.drawable.CategoryWeight;

/*
   CategoryWeightF extends CategorySummaryF which extends CategoryRatios
*/
public class CategoryWeightF extends CategorySummaryF
{
    public  static final int         BYTESIZE        = CategorySummaryF.BYTESIZE
                                                     + 4;  // type_idx

    public  static final Comparator  INDEX_ORDER     = new IndexOrder();

    private static final int INVALID_INDEX = Integer.MIN_VALUE;

    private int        type_idx;
    private Category   type;

    public CategoryWeightF()
    {
        super();
        type           = null;
        type_idx       = INVALID_INDEX;
    }

    // For SLOG-2 Output
    public CategoryWeightF( final Category new_type,
                            float new_incl_r, float new_excl_r,
                            double new_num_real_objs )
    {
        super( new_incl_r, new_excl_r, new_num_real_objs );
        type           = new_type;
        type_idx       = type.getIndex();
    }

    // For SLOG-2 Output
    public CategoryWeightF( final CategoryWeightF type_wgf )
    {
        super( type_wgf );
        this.type           = type_wgf.type;
        this.type_idx       = type_wgf.type_idx;
    }

    // For Jumpshot, copy construct,  CategoryWeight -> CategoryWeightF.
    public CategoryWeightF( final CategoryWeight type_wgt )
    {
        super( type_wgt );
        this.type           = type_wgt.getCategory();
        this.type_idx       = this.type.getIndex();
    }

    public Category getCategory()
    {
        return type;
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
            CategoryWeightF type_wgf1 = (CategoryWeightF) o1;
            CategoryWeightF type_wgf2 = (CategoryWeightF) o2;
            return type_wgf1.type_idx - type_wgf2.type_idx;
        }
    }
}

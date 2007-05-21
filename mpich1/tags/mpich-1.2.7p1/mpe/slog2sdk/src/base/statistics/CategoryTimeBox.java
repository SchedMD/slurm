/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import java.awt.Color;
import java.util.Comparator;

import base.drawable.TimeBoundingBox;

public class CategoryTimeBox extends TimeBoundingBox
{
    public static final Comparator  INDEX_ORDER       = new IndexOrder();
    public static final Comparator  INCL_RATIO_ORDER  = new InclRatioOrder();
    public static final Comparator  EXCL_RATIO_ORDER  = new ExclRatioOrder();
    public static final Comparator  COUNT_ORDER       = new CountOrder();

    private CategoryWeightF   twgf;
    
    public CategoryTimeBox()
    {
        super();
        twgf  = null;
    }

    public CategoryTimeBox( final CategoryWeightF  in_twgf )
    {
        super();
        twgf  = in_twgf;
    }

    public float  getCategoryRatio( boolean isInclusive )
    {
        return twgf.getRatio( isInclusive );
    }

    public Color  getCategoryColor()
    {
        return twgf.getCategory().getColor();
    }

    public boolean  isCategoryVisiblySearchable()
    {
        return twgf.getCategory().isVisiblySearchable();
    }

    public CategoryWeightF  getCategoryWeightF()
    {
        return twgf;
    }


    private static class IndexOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryTimeBox typebox1 = (CategoryTimeBox) o1;
            CategoryTimeBox typebox2 = (CategoryTimeBox) o2;
            return CategoryWeightF.INDEX_ORDER.compare( typebox1.twgf,
                                                        typebox2.twgf );
        }
    }

    private static class InclRatioOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryTimeBox typebox1 = (CategoryTimeBox) o1;
            CategoryTimeBox typebox2 = (CategoryTimeBox) o2;
            return CategoryWeightF.INCL_RATIO_ORDER.compare( typebox1.twgf,
                                                             typebox2.twgf );
        }
    }

    private static class ExclRatioOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryTimeBox typebox1 = (CategoryTimeBox) o1;
            CategoryTimeBox typebox2 = (CategoryTimeBox) o2;
            return CategoryWeightF.EXCL_RATIO_ORDER.compare( typebox1.twgf,
                                                             typebox2.twgf );
        }
    }

    private static class CountOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryTimeBox typebox1 = (CategoryTimeBox) o1;
            CategoryTimeBox typebox2 = (CategoryTimeBox) o2;
            return CategoryWeightF.COUNT_ORDER.compare( typebox1.twgf,
                                                        typebox2.twgf );
        }
    }
}

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

public class CategoryRatios implements DataIO
{
    public  static final int         BYTESIZE          = 4   // incl_ratio
                                                       + 4;  // excl_ratio

    public  static final Comparator  INCL_RATIO_ORDER  = new InclRatioOrder();
    public  static final Comparator  EXCL_RATIO_ORDER  = new ExclRatioOrder();

    public  static final int         PRINT_ALL_RATIOS  = 0;
    public  static final int         PRINT_INCL_RATIO  = 1;
    public  static final int         PRINT_EXCL_RATIO  = 2;

    private static final String      TITLE_ALL_RATIOS
                                     = "*** All Duration Ratios:";
    private static final String      TITLE_INCL_RATIO
                                     = "*** Inclusive Duration Ratio:";
    private static final String      TITLE_EXCL_RATIO
                                     = "*** Exclusive Duration Ratio:";

    private float      incl_ratio;
    private float      excl_ratio;

    public CategoryRatios()
    {
        incl_ratio     = 0.0f;
        excl_ratio     = 0.0f;
    }

    // For SLOG-2 Output
    public CategoryRatios( float new_incl_r, float new_excl_r )
    {
        incl_ratio     = new_incl_r;
        excl_ratio     = new_excl_r;
    }

    // For SLOG-2 Output
    public CategoryRatios( final CategoryRatios type_rts )
    {
        this.incl_ratio     = type_rts.incl_ratio;
        this.excl_ratio     = type_rts.excl_ratio;
    }

    public float getRatio( boolean isInclusive )
    {
        if ( isInclusive )
            return incl_ratio;
        else
            return excl_ratio;
    }

    public void rescaleAllRatios( float ftr )
    {
        incl_ratio *= ftr;
        excl_ratio *= ftr;
    }

    public void addAllRatios( final CategoryRatios a_type_rts, float ftr )
    {
        this.incl_ratio += a_type_rts.incl_ratio * ftr;
        this.excl_ratio += a_type_rts.excl_ratio * ftr;
    }

    public void addExclusiveRatio( float extra_ratio )
    {
        this.excl_ratio += extra_ratio;
    }

    // For Jumpshot-4
    public void addInclusiveRatio( float extra_ratio )
    {
        this.incl_ratio += extra_ratio;
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeFloat( incl_ratio );
        outs.writeFloat( excl_ratio );
    }

    public CategoryRatios( DataInput ins )
    throws java.io.IOException
    {
        super();
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        incl_ratio     = ins.readFloat();
        excl_ratio     = ins.readFloat();
    }

    // For InfoPanelForDrawable
    public static String getPrintTitle( int print_status )
    {
        if ( print_status == PRINT_INCL_RATIO )
            return TITLE_INCL_RATIO;
        else if ( print_status == PRINT_EXCL_RATIO )
            return TITLE_EXCL_RATIO;
        else // if ( print_status == PRINT_ALL_RATIOS )
            return TITLE_ALL_RATIOS;
    }

    // For InfoPanelForDrawable
    public String toInfoBoxString( int print_status )
    {
        if ( print_status == PRINT_INCL_RATIO )
            return "ratio=" + incl_ratio;
        else if ( print_status == PRINT_EXCL_RATIO )
            return "ratio=" + excl_ratio;
        else // if ( print_status == PRINT_ALL_RATIOS )
            return "incl_ratio=" + incl_ratio + ", excl_ratio=" + excl_ratio;
    }

    public String toString()
    {
        return "ratios=" + incl_ratio + "," + excl_ratio;
    }



    private static class InclRatioOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryRatios type_rts1 = (CategoryRatios) o1;
            CategoryRatios type_rts2 = (CategoryRatios) o2;
            float diff = type_rts1.incl_ratio - type_rts2.incl_ratio;
            return ( diff < 0.0f ? -1 : ( diff == 0.0f ? 0 : 1 ) );
        }
    }

    private static class ExclRatioOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            CategoryRatios type_rts1 = (CategoryRatios) o1;
            CategoryRatios type_rts2 = (CategoryRatios) o2;
            float diff = type_rts1.excl_ratio - type_rts2.excl_ratio;
            return ( diff < 0.0f ? -1 : ( diff == 0.0f ? 0 : 1 ) );
        }
    }
}

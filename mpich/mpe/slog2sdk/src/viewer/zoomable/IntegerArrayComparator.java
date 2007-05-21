/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.util.Comparator;

public class IntegerArrayComparator implements Comparator
{
    public int compare( Object o1, Object o2 )
    {
        Integer[] ary1, ary2;
        ary1 = (Integer[]) o1;
        ary2 = (Integer[]) o2;

        int ary1_len = ary1.length;
        int ary2_len = ary2.length;
        int min_len = ary1_len > ary2_len ? ary2_len : ary1_len;

        int result = 0;
        for ( int idx = 0; idx < min_len; idx++ ) {
            result = ary1[ idx ].intValue() - ary2[ idx ].intValue(); 
            if ( result != 0 )
                return result;
        }

        if ( ary1_len == ary2_len )
            return 0;
        else
            return ary1_len > ary2_len ? 1 : -1;
    }

    public static final void main( String[] args )
    {
        Integer[] iary1 = new Integer[] { new Integer(2), new Integer(3),
                                          new Integer(0) };
        Integer[] iary2 = new Integer[] { new Integer(2), new Integer(4) };

        IntegerArrayComparator comp = new IntegerArrayComparator();
        System.out.println( comp.compare( iary1, iary2 ) );
    }
}

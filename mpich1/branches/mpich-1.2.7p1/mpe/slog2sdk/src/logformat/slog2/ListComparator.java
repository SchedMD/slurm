/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

import java.util.*;

public class ListComparator implements Comparator
{
    public int compare( Object o1, Object o2 )
    {
        int result = 0;
        Iterator itr1, itr2;
        itr1 = ( (List) o1 ).iterator();
        itr2 = ( (List) o2 ).iterator();
        while ( result == 0 && itr1.hasNext() && itr2.hasNext() )
            result = ( (Comparable) itr1.next() ).compareTo( itr2.next() );
        if ( ! itr1.hasNext() && ! itr2.hasNext() )
            return result;
        else {
            if ( result != 0 )
                return result;
            else {
                if ( itr2.hasNext() )
                    return 1;
                else
                    return -1;
            }
        }
    }

    public static final void main( String[] args )
    {
        final int keysize = 64;
        // Map map = new TreeMap( new ListComparator() );
        Map map = new HashMap();
        List key;
        String classkey, value;
        int ii, jj, kk;
        int dkk, djj, dii;

        String[] classkeys = new String[] { "State", "Arrow", "Collective" };

        for ( kk = 0; kk < classkeys.length; kk++ ) {
            for ( jj = 1; jj < keysize; jj++ ) {
                for ( ii = 1; ii < keysize; ii++ ) {
                    key = new ArrayList();
                    key.add( classkeys[ kk ] );
                    key.add( new Integer( ii ) );
                    key.add( new Integer( jj ) );
                    value = classkeys[ kk ].toUpperCase() + jj + ii;
                    map.put( key, value );
                }
            }
        }

        /*
        Iterator itr = map.entrySet().iterator();
        while ( itr.hasNext() )
            System.out.println( itr.next() );
        */

        List keys[] = new List[ 10 ];
        kk  = 0; ii  = 0; jj = 1;
        dkk = 1; dii = 5; djj= 7;
        for ( int idx = 0; idx < keys.length; idx++ ) {
            keys[ idx ] = new ArrayList();
            keys[ idx ].add( classkeys[ kk ] );
            keys[ idx ].add( new Integer( ii ) );
            keys[ idx ].add( new Integer( jj ) );
            kk += dkk; kk %= classkeys.length;
            ii += dii; ii %= keysize;
            jj += djj; jj %= keysize;
        }

        System.out.println( "\ntesting...." );
        Date time1 = new Date();
        for ( int idx = 0; idx < 10000; idx++ ) {
            key = keys[ idx % keys.length ];
            // System.out.println( "key=" + key + ", value=" + map.get( key ) );
            value = (String) map.get( key );
        }
        Date time2 = new Date();

        System.out.println( "\nTiming :" );
        // System.err.println( "time1 = " + time1 + ", " + time1.getTime() );
        // System.err.println( "time2 = " + time2 + ", " + time2.getTime() );
        System.err.println( "timeElapsed between 1 & 2 = "
                          + ( time2.getTime() - time1.getTime() ) + " msec" );
    }
}

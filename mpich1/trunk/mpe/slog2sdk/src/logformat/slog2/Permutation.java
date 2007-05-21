/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Enumeration;

public class Permutation implements Enumeration
{
    private int    numLevels;
    private int    numChildrenPerNode;

    private long   enum_idx;
    private long   enum_max;
    private int[]  next_enum;

    public Permutation( int Nlevels, int Nchilds )
    {
        numLevels           = Nlevels;
        numChildrenPerNode  = Nchilds;
        next_enum           = new int[ numLevels ];
        for ( int idx = 0; idx < numLevels; idx++ )
            next_enum[ idx ] = 0;
        enum_max            = (long) Math.pow( (double) Nchilds,
                                               (double) Nlevels );
        enum_idx            = 0;
    }

    public boolean hasMoreElements()
    {
        return enum_idx < enum_max;
    }

    public Object nextElement()
    {
        int[] curr_enum = (int[]) next_enum.clone();

        // Update next_enum
        for ( int ilevel = 0; ilevel < numLevels; ilevel++ ) {
            next_enum[ ilevel ]++;
            if ( next_enum[ ilevel ] < numChildrenPerNode )
                break;
            else
                next_enum[ ilevel ] = 0;
        }
        enum_idx++;

        return curr_enum;
    }


    public static final void main( String[] args )
    {
        int Nlevels = Integer.parseInt( args[ 0 ] );
        int Nchilds = Integer.parseInt( args[ 1 ] );

        int[] icfg;
        Enumeration perms = new Permutation( Nlevels, Nchilds );
        while ( perms.hasMoreElements() ) {
            icfg = (int[]) perms.nextElement();
            for ( int idx = 0 ; idx < icfg.length; idx++ )
                System.out.print( icfg[ idx ] + " " );
            System.out.println();
        }

    }
}

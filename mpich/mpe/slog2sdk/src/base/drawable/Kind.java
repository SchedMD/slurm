/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

public class Kind
{
    //  The XXXX_ID is for wrapper, trace.peekNextKind()
    public static final int   TOPOLOGY_ID   = -1;
    public static final int   EOF_ID        = 0;
    public static final int   PRIMITIVE_ID  = 1;
    public static final int   COMPOSITE_ID  = 2;
    public static final int   CATEGORY_ID   = 3;
    public static final int   YCOORDMAP_ID  = 4;

    public static final Kind  TOPOLOGY      = new Kind( TOPOLOGY_ID );
    public static final Kind  EOF           = new Kind( EOF_ID );
    public static final Kind  PRIMITIVE     = new Kind( PRIMITIVE_ID );
    public static final Kind  COMPOSITE     = new Kind( COMPOSITE_ID );
    public static final Kind  CATEGORY      = new Kind( CATEGORY_ID );
    public static final Kind  YCOORDMAP     = new Kind( YCOORDMAP_ID );

    private int  index;

    public Kind( int idx )
    {
        index = idx;
    }

    public boolean equals( final Kind aKind )
    {
        return this.index == aKind.index;
    }

    public boolean equals( Object obj )
    {
        return this.equals( (Kind) obj );
    }

    public int hashCode()
    {
        return index;
    }

    public String toString()
    {
        switch ( index ) {
            case TOPOLOGY_ID :
                return "Topology";
            case EOF_ID :
                return "EndOfFile";
            case PRIMITIVE_ID :
                return "Primitive";
            case COMPOSITE_ID :
                return "Composite";
            case CATEGORY_ID :
                return "Category";
            case YCOORDMAP_ID :
                return "YCoordMap";
            default :
                return "Unknown Index = " + index;
        }
    }
}

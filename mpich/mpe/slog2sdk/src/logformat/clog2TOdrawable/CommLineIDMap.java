/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import java.util.Map;
import java.util.TreeMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;

import base.drawable.Coord;
import base.drawable.Primitive;
import base.drawable.YCoordMap;

/*
   Define a TreeMap where the order is determined by lineID in CommLineID class.
   The object that use this map is drawable whose lineID field is int and
   not an Object.  In order to avoid creating Integer Object of lineID
   for every drawable from clog2 file to search CommLineID in this class,
   we will use Coord as key becayse lineID field in drawable is embedded
   within its Coord member.  The class will use Comparator in
   Coord's LINEID_ORDER to sort/search lineID.
*/
public class CommLineIDMap extends TreeMap
{
    public CommLineIDMap()
    {
        super( Coord.LINEID_ORDER );
    }

    public void addCommLineID( CommLineID commlineID )
    {
        super.put( new Coord( 0.0, commlineID.lineID ), commlineID );
    }

    public void setCommLineIDUsed( Primitive  prime )
    {
        CommLineID  commlineID;

        /* State and Event only need to have its 1st vertex checked */
        commlineID = (CommLineID) super.get( prime.getStartVertex() );
        if ( commlineID != null )
            commlineID.setUsed( true );
        if ( prime.getCategory().getTopology().isArrow() ) {
            commlineID = (CommLineID) super.get( prime.getFinalVertex() );
            if ( commlineID != null )
                commlineID.setUsed( true );
        }
    }

    public void initialize() {}

    // Cannot use finalize() as function name,
    // finalize() overrides Object.finalize().
    public void finish()
    {
        CommLineID  commlineID;
        // Remove any "unUsed" CommLineID elements from the map;
        Iterator commlineID_itr  = super.values().iterator();
        while ( commlineID_itr.hasNext() ) {
            commlineID = (CommLineID) commlineID_itr.next();
            if ( ! commlineID.isUsed() )
                commlineID_itr.remove();
        }
    }

    public List createYCoordMapList()
    {
        int         world_elems[], comm_elems[];
        CommLineID  commlineID;
        int         num_rows, num_cols4world, num_cols4comm;
        int         world_idx, comm_idx;

        // Set number of rows of int[]
        num_rows       = super.size();

        // world_elems[] has 2 columns: lineID->wrank
        num_cols4world = 2;
        world_idx      = 0;
        world_elems    = new int[ num_rows * num_cols4world ];

        // comm_elems[] has 3 columns: lineID ->(icomm,rank)
        num_cols4comm  = 3;
        comm_idx       = 0;
        comm_elems     = new int[ num_rows * num_cols4comm ];

        // Setting the world_elems[] and comm_elems[]
        Iterator   commlineID_itr = super.values().iterator();
        while ( commlineID_itr.hasNext() ) {
            commlineID = (CommLineID) commlineID_itr.next();
            world_elems[ world_idx++ ] = commlineID.lineID;
            world_elems[ world_idx++ ] = commlineID.wrank;
            comm_elems[ comm_idx++ ]   = commlineID.lineID;
            comm_elems[ comm_idx++ ]   = commlineID.icomm;
            comm_elems[ comm_idx++ ]   = commlineID.rank;
        }

        // Create CommWorldViewMap
        YCoordMap  world_viewmap;
        world_viewmap = new YCoordMap( num_rows, num_cols4world,
                                       "CommWorld View",
                                       new String[] {"world_rank"},
                                       world_elems, null );

        // Create CommWorldViewMap
        YCoordMap   comm_viewmap;
        comm_viewmap = new YCoordMap( num_rows, num_cols4comm,
                                      "Multi-Communicator View",
                                      new String[] {"commID", "rank"},
                                      comm_elems, null );

        List  ycoordmap_list = new ArrayList( 2 );
        ycoordmap_list.add( world_viewmap );
        ycoordmap_list.add( comm_viewmap );

        return ycoordmap_list;
    }
}

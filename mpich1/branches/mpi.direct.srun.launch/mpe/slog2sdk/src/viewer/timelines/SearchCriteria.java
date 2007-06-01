/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.util.Arrays;

import base.drawable.Drawable;
import viewer.zoomable.YaxisTree;

public class SearchCriteria
{
    private YaxisTree   tree_view;
    private int[]       selected_rowIDs;
    private boolean     searchAllRows;

    public SearchCriteria( final YaxisTree y_tree )
    {
        tree_view = y_tree;
    }

    public void initMatch()
    {
        selected_rowIDs = tree_view.getSelectionRows();
        searchAllRows = ( selected_rowIDs != null ?
                          selected_rowIDs.length == 0 : true );
        if ( ! searchAllRows ) {
            Arrays.sort( selected_rowIDs );
        }
    }

    public boolean isMatched( Drawable dobj )
    {
        if ( searchAllRows )
            return true;
        else
            return ( Arrays.binarySearch( selected_rowIDs, dobj.getRowID() )
                     >= 0 );
    }
}

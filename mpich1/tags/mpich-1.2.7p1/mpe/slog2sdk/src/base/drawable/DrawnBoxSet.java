/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import javax.swing.JTree;

public class DrawnBoxSet
{
    private        JTree       tree_view;
    private        DrawnBox[]  last_arrow_pos;
    private        DrawnBox[]  last_state_pos;
    private        DrawnBox[]  last_event_pos;

    private        int      num_rows;

    public DrawnBoxSet( final  JTree in_tree_view )
    {
        tree_view        = in_tree_view;
        last_arrow_pos   = null;
        last_state_pos   = null;
        last_event_pos   = null;
        num_rows         = 0;
    }

    public void initialize()
    {
        int     ii, jj, idx;
        //  Need to check to see if tree_view has been updated,
        //  If not, no need to construct all these Stack[].
        num_rows         = tree_view.getRowCount();
        last_arrow_pos   = new DrawnBox[ num_rows * num_rows ];
        last_state_pos   = new DrawnBox[ num_rows ];
        last_event_pos   = new DrawnBox[ num_rows ];

        idx = -1;
        for ( ii = 0 ; ii < num_rows ; ii++ ) {
            //  Select only non-expanded row
            if ( ! tree_view.isExpanded( ii ) ) {
                last_state_pos[ ii ] = new DrawnBox();
                last_event_pos[ ii ] = new DrawnBox();
            }
            else {
                last_state_pos[ ii ] = null;
                last_event_pos[ ii ] = null;
            }

            for ( jj = 0 ; jj < num_rows ; jj++ ) {
                // idx = num_rows * ii + jj;
                idx++;
                if (    ! tree_view.isExpanded( ii ) 
                     && ! tree_view.isExpanded( jj ) )
                    last_arrow_pos[ idx ] = new DrawnBox();
                else
                    last_arrow_pos[ idx ] = null;
            }
        }
    }

    public void reset()
    {
        int     ii, jj, idx;
        idx = -1;
        for ( ii = 0 ; ii < num_rows ; ii++ ) {
            //  Select only non-expanded row
            if ( last_state_pos[ ii] != null ) {
                last_state_pos[ ii ].reset();
                last_event_pos[ ii ].reset();
            }

            for ( jj = 0 ; jj < num_rows ; jj++ ) {
                // idx = num_rows * ii + jj;
                idx++;
                if ( last_arrow_pos[ idx ] != null )
                    last_arrow_pos[ idx ].reset();
            }
        }
    }

    // Cannot use finalize() as function name,
    // finalize() overrides Object.finalize().
    public void finish()
    {
        int     ii, jj, idx;
        idx = -1;
        for ( ii = 0 ; ii < num_rows ; ii++ ) {
            //  Select only non-expanded row
            if ( last_state_pos[ ii] != null ) {
                last_state_pos[ ii ] = null;
                last_event_pos[ ii ] = null;
            }

            for ( jj = 0 ; jj < num_rows ; jj++ ) {
                // idx = num_rows * ii + jj;
                idx++;
                if ( last_arrow_pos[ idx ] != null )
                    last_arrow_pos[ idx ] = null;
            }
        }
    }

    public DrawnBox getLastArrowPos( int ii, int jj )
    {
        return last_arrow_pos[ num_rows * ii + jj ];
    }

    public DrawnBox getLastStatePos( int ii )
    {
        return last_state_pos[ ii ];
    }

    public DrawnBox getLastEventPos( int ii )
    {
        return last_event_pos[ ii ];
    }
}

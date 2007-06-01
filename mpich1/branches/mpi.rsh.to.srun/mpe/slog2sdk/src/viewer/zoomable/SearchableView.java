/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.Rectangle;

import base.drawable.Drawable;

/*
   Define the interface to be implemented by the view object, ScrollableView,
   so that it is searchable in time
*/

public interface SearchableView
{
    public Rectangle localRectangleForDrawable( final Drawable dobj );

    // NEW search starting from the specified time
    public SearchPanel searchPreviousComponent( double searching_time );

    // CONTINUING search
    public SearchPanel searchPreviousComponent();

    // NEW search starting from the specified time
    public SearchPanel searchNextComponent( double searching_time );

    // CONTINUING search
    public SearchPanel searchNextComponent();

}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import javax.swing.JViewport;

/*
   Define the interface to be implemented by the view object within
   the viewport.  The interface defines the operations of view object
   to be used by the viewport.
*/

public interface ScrollableView
{
    public void checkToZoomView();

    public void checkToScrollView();

    public int  getXaxisViewPosition();

    public void componentResized( JViewport viewport );

    // public void setJComponentSize();
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import javax.swing.JPanel;

import base.drawable.Drawable;

/*
    Class implementing this interface has to be java.awt.Component
*/
public abstract class SearchPanel extends JPanel
{
     public abstract Drawable  getSearchedDrawable();
}

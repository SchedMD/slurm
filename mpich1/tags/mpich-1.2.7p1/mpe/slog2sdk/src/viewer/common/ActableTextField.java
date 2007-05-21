/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import javax.swing.JTextField;

public class ActableTextField extends JTextField
{
    public ActableTextField()
    { super(); }

    // For logname_fld in FirstPanel
    public ActableTextField( String name, int icolumn )
    { super( name, icolumn ); }

    public void fireActionPerformed()
    { super.fireActionPerformed(); }
}

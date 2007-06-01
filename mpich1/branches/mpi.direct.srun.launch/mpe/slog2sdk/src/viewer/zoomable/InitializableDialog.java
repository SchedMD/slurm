/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.Dialog;
import javax.swing.JDialog;

public abstract class InitializableDialog extends JDialog
{
    public InitializableDialog( Dialog dialog, String str )
    { super( dialog, str ); }

    public abstract void init();
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import javax.swing.*;
import java.awt.*;

public class Dialogs
{
    public static void error( Component p, String txt )
    {
       JOptionPane.showMessageDialog( p, txt, "Error",
                                      JOptionPane.ERROR_MESSAGE );
    }

    public static void warn( Component p, String txt )
    {
       JOptionPane.showMessageDialog( p, txt, "Warning",
                                      JOptionPane.WARNING_MESSAGE );
    }

    public static boolean confirm( Component p, String txt )
    {
        int ans = JOptionPane.showConfirmDialog( p, txt, "Confirmation",
                                                 JOptionPane.YES_NO_OPTION );
        return ( ans == JOptionPane.YES_OPTION );
    }   

    public static void info( Component p, String txt, ImageIcon icon )
    {
        if ( icon != null ) 
            JOptionPane.showMessageDialog( p, txt, "Information",
                                           JOptionPane.INFORMATION_MESSAGE,
                                           icon );
        else
            JOptionPane.showMessageDialog( p, txt, "Information",
                                           JOptionPane.INFORMATION_MESSAGE );
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import javax.swing.JTextArea;

public class AdvancingTextArea extends JTextArea
{
    public AdvancingTextArea()
    { super(); }

    public void append( String str )
    {
        super.append( str );
        super.setCaretPosition( super.getDocument().getLength() );
    }
}

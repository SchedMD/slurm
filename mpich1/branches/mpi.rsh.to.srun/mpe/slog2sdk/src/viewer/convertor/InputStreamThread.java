/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.IOException;

public class InputStreamThread extends Thread
{
    private InputStream          ins;
    private String               prefix;
    private AdvancingTextArea    textarea;

    private boolean              isRunning;

    public InputStreamThread( InputStream         the_ins,
                              String              the_prefix,
                              AdvancingTextArea   the_textarea )
    {
        ins       = the_ins;
        prefix    = the_prefix;
        textarea  = the_textarea;

        isRunning = true;
    }

    public void stopRunning()
    {
        isRunning = false;
    }

    public void run()
    {
        try {
            String  line = null;
            InputStreamReader ins_rdr = new InputStreamReader( ins );
            BufferedReader    buf_rdr = new BufferedReader( ins_rdr );
            while ( isRunning && ( line = buf_rdr.readLine() ) != null )
                textarea.append( "\n" + prefix + " > " + line );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
        }
    }
}

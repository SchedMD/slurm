/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog;

public class RecDefMsg extends RecDefState
{
    public RecDefMsg()
    {
        super();
        super.stateID      = -1;
        super.startetype   = new Integer( Const.MsgType.SEND );
        super.endetype     = new Integer( Const.MsgType.RECV );
        super.color        = "white";
        super.description  = "message";
    }
}

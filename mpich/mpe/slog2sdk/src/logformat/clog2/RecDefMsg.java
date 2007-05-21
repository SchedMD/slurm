/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

public class RecDefMsg extends RecDefState
{
    public RecDefMsg()
    {
        super();
        super.stateID      = -1;
        super.startetype   = new Integer( Const.MsgType.SEND );
        super.finaletype   = new Integer( Const.MsgType.RECV );
        super.color        = "white";
        super.name         = "message";
        super.format       = "msg_tag=%d, msg_size=%d";
    }
}

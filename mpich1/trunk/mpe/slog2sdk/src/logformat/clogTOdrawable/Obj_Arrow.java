/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clogTOdrawable;

import java.io.DataOutputStream;
import base.io.BufArrayOutputStream;
import base.drawable.Coord;
import base.drawable.Category;
// import base.drawable.InfoType;
import base.drawable.Primitive;

public class Obj_Arrow extends Primitive
{
    private  int       msg_tag;
    private  int       msg_size; 

    public Obj_Arrow()
    {
        super( 2 );
    }

    public Obj_Arrow( final Category obj_type )
    {
        super( obj_type, 2 );
    }

    public Obj_Arrow( final Category obj_type,
                      final Coord  start_vtx, final Coord  final_vtx )
    {
        super( obj_type, 2 );
        super.setStartVertex( start_vtx );
        super.setFinalVertex( final_vtx );
    }

    public void setMsgTag( int in_msg_tag )
    {
        this.msg_tag  = in_msg_tag;
    }

    public int getMsgTag()
    {
        return this.msg_tag;
    }

    public void setMsgSize( int in_msg_size )
    {
        this.msg_size  = in_msg_size;
    }

    public int getMsgSize()
    {
        return this.msg_size;
    }

    public void setInfoBuffer()
    {
        BufArrayOutputStream bary_outs  = new BufArrayOutputStream( 8 );
        DataOutputStream     data_outs  = new DataOutputStream( bary_outs );
        try {
            data_outs.writeInt( this.msg_tag );
            data_outs.writeInt( this.msg_size );
        } catch ( java.io.IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }
        super.setInfoBuffer( bary_outs.getByteArrayBuf() );
    }

    public String toString()
    {
        return ( "Arrow{ " + super.toString() + " }" );
    }
}

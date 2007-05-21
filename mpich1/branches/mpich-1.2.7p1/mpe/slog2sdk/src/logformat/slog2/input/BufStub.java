/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.input;

import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.io.DataInput;
import java.io.DataOutput;

import base.drawable.TimeBoundingBox;
import base.drawable.Primitive;
import logformat.slog2.*;

/*  
    Buffer Signature object for Natural Ordering.  
    It is for debugging purposes as well.
 */
public class BufStub extends BufForObjects
{
    private BufForObjects buf4objs;
    private int           Nprimes = -1;
    private int           Nobjs   = -1;
    private int           bsize   = -1;

    public BufStub( final BufForObjects in_buf4objs )
    {
        super();
        buf4objs = in_buf4objs;
        super.affectTimeBounds( buf4objs );
        super.setTreeNodeID( buf4objs.getTreeNodeID() );
        super.setFileBlockPtr( buf4objs.getFilePointer(),
                               buf4objs.getBlockSize() );
    }

    public int  getByteSize()
    { 
        System.err.println( "BufStub.getByteSize(): should NOT be called" );
        return bsize;
    }

    public void add( final Primitive prime )
    {
        System.err.println( "BufStub.add(): should NOT be called" );
    }

    public void empty()
    {
        System.err.println( "BufStub.empty(): should NOT be called" );
    }

    public int  getNumOfDrawables()
    { 
        System.err.println( "BufStub.getNumOfDrawables(): "
                          + "should NOT be called" );
        return Nobjs;
    }

    public int  getNumOfPrimitives()
    { 
        System.err.println( "BufStub.getNumOfPrimitives(): "
                          + "should NOT be called" );
        return Nprimes;
    }

    public LineIDMap getIdentityLineIDMap()
    { 
        System.err.println( "BufStub.getILineIDMap(): should NOT be called" );
        return null;
    }

    public Iterator nestableBackIterator( final TimeBoundingBox tframe,
                                                boolean         isComposite )
    { 
        System.err.println( "BufStub.nestableBackIterator(): "
                          + "should NOT be called" );
        return null;
    }

    public Iterator nestlessBackIterator( final TimeBoundingBox tframe )
    { 
        System.err.println( "BufStub.nestlessBackIterator(): "
                          + "should NOT be called" );
        return null;
    }

    public Iterator nestableForeIterator( final TimeBoundingBox tframe,
                                                boolean         isComposite )
    { 
        System.err.println( "BufStub.nestableForeIterator(): "
                          + "should NOT be called" );
        return null;
    }

    public Iterator nestlessForeIterator( final TimeBoundingBox tframe )
    { 
        System.err.println( "BufStub.nestableForeIterator(): "
                          + "should NOT be called" );
        return null;
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "{ " );
        rep.append( super.toString() );
        // if ( Nprimes < 0 )
        //     Nprimes  = buf4objs.getNumOfPrimitives();
        // rep.append( " Nprimes=" + Nprimes );
        if ( Nobjs < 0 )
            Nobjs    = buf4objs.getNumOfDrawables();
        rep.append( " Nobjs=" + Nobjs );
        if ( bsize < 0 )
            bsize    = buf4objs.getByteSize();
        rep.append( " bsize=" + bsize );
        rep.append( " }" );
        return rep.toString();
    }
}

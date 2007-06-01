/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.ListIterator;
import java.util.Collections;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Composite;

public class BufForDrawables extends BufForObjects
{
    private static final int  INIT_BYTESIZE = BufForObjects.BYTESIZE
                                            + 4  /* buf4nestable.size() */
                                            + 4  /* buf4nestless.size() */ ;

    private static final byte PRIMITIVE_ID  = 0;
    private static final byte COMPOSITE_ID  = 1;

    /*
        nestable = drawables that can be nested nicely, like state-like object
        nestless = drawables that need no nesting order, e.g. arrow/event
    */
    private List              buf4nestable;   /* state and composite */
    private List              buf4nestless;   /* arrow/event */
    private Drawable.Order    buf4dobj_order; /* buf4nestxxx's storage order */

    /*  
        isOutputBuf = true  when BufForDrawables is used in Output API
        isOutputBuf = false when BufForDrawables is used in Input API
    */
    private boolean           isOutputBuf;
    private boolean           haveObjectsBeenSaved;
    private int               total_bytesize; // bytesize for the disk footprint

    public BufForDrawables( boolean isForOutput )
    {
        super();
        isOutputBuf          = isForOutput;
        // TRACE-API passes drawables in Drawable.INCRE_FINALTIME_ORDER.
        // At writeObject(), drawables are saved in INCRE_STARTTIME_ORDER.
        // At readObject(), drawables are read/stored in INCRE_STARTTIME_ORDER.
        if ( isOutputBuf ) {
            buf4nestable       = new ArrayList();
            buf4nestless       = new ArrayList();
            buf4dobj_order     = Drawable.INCRE_FINALTIME_ORDER;
        }
        else {
            buf4nestable       = null;
            buf4nestless       = null;
            buf4dobj_order     = Drawable.INCRE_STARTTIME_ORDER;
        }

        haveObjectsBeenSaved = false;
        total_bytesize       = INIT_BYTESIZE;
    }

    public int getByteSize()
    {
        return total_bytesize;
    }

    public void add( final Primitive prime )
    {
        if ( prime.getCategory().getTopology().isState() )
            buf4nestable.add( prime );
        else
            buf4nestless.add( prime );
        // Extra 1 byte indicates if the Drawable is Primitive/Composite
        total_bytesize += ( prime.getByteSize() + 1 );
    }

    public void add( final Composite cmplx )
    {
        // assume all Composites contain state-like object
        buf4nestable.add( cmplx );
        // Extra 1 byte indicates if the Drawable is Primitive/Composite
        total_bytesize += ( cmplx.getByteSize() + 1 );
    }

    // For SLOG-2 Output API
    public void empty()
    {
        if ( haveObjectsBeenSaved ) {
            buf4nestable.clear();
            buf4nestless.clear();
            buf4dobj_order       = Drawable.INCRE_FINALTIME_ORDER;
            haveObjectsBeenSaved = false;
            total_bytesize       = INIT_BYTESIZE;
        }
    }

    public int getNumOfDrawables()
    {
        return buf4nestable.size() + buf4nestless.size();
    }

    /*  This is expensive compared to getNumOfDrawables, avoid calling this  */
    public int getNumOfPrimitives()
    {
        int       count;
        Iterator  dobjs_itr;
        // assume buf4nestless contains only primitives, e.g. arrow/event
        count     = buf4nestless.size();
        dobjs_itr = buf4nestable.iterator();
        while ( dobjs_itr.hasNext() )
            count += ( (Drawable) dobjs_itr.next() ).getNumOfPrimitives();
        return count;
    }

    // Iterator of Nestable Drawables in Increasing StartTime order
    public Iterator nestableForeIterator( final TimeBoundingBox tframe,
                                                boolean         isComposite )
    {
        if ( isComposite )
            return new IteratorOfForeDrawables( buf4nestable, tframe );
        else
            return new IteratorOfForePrimitives( buf4nestable, tframe );
    }
    
    // Iterator of Nestless Drawables in Increasing StartTime order
    public Iterator nestlessForeIterator( final TimeBoundingBox tframe )
    {
        return new IteratorOfForeDrawables( buf4nestless, tframe );
    }

    // Iterator of Nestable Drawables in Decreasing StartTime order
    public Iterator nestableBackIterator( final TimeBoundingBox tframe,
                                                boolean         isComposite )
    {
        if ( isComposite )
            return new IteratorOfBackDrawables( buf4nestable, tframe );
        else
            return new IteratorOfBackPrimitives( buf4nestable, tframe );
    }

    // Iterator of Nestless Drawables in Decreasing StartTime order
    public Iterator nestlessBackIterator( final TimeBoundingBox tframe )
    {
        return new IteratorOfBackDrawables( buf4nestless, tframe );
    }

    public LineIDMap getIdentityLineIDMap()
    {
        List buf4drawables = new ArrayList( buf4nestable );
        buf4drawables.addAll( buf4nestless );
        return super.toIdentityLineIDMap( buf4drawables );
    }


    // For SLOG-2 Input/Output API
    public void reorderDrawables( final Drawable.Order dobj_order )
    {
        if ( ! buf4dobj_order.equals( dobj_order ) ) {
            buf4dobj_order = dobj_order;
            // Save the Lists in the specified Drawable.Order
            Collections.sort( buf4nestable, buf4dobj_order );
            Collections.sort( buf4nestless, buf4dobj_order );
        }
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        ListIterator dobjs_itr;
        Drawable     dobj;
        int          Nobjs;

        super.writeObject( outs );   // BufForObjects.writeObject( outs )

        // Save the Lists in Increasing Starttime order
        this.reorderDrawables( Drawable.INCRE_STARTTIME_ORDER );

        // assume buf4nestless contains only primitives, e.g. arrow/event
        Nobjs  = buf4nestless.size();
        outs.writeInt( Nobjs );
        dobjs_itr = buf4nestless.listIterator( 0 );
        while ( dobjs_itr.hasNext() ) {
            dobj = (Drawable) dobjs_itr.next();
            if ( dobj instanceof Composite ) {
                outs.writeByte( (int) COMPOSITE_ID );
                ( (Composite) dobj ).writeObject( outs );
            }
            else {
                outs.writeByte( (int) PRIMITIVE_ID );
                ( (Primitive) dobj ).writeObject( outs );
            }
        }

        // assume buf4nestable contains both primitives and composites.
        Nobjs  = buf4nestable.size();
        outs.writeInt( Nobjs );
        dobjs_itr = buf4nestable.listIterator( 0 );
        while ( dobjs_itr.hasNext() ) {
            dobj = (Drawable) dobjs_itr.next();
            if ( dobj instanceof Composite ) {
                outs.writeByte( (int) COMPOSITE_ID );
                ( (Composite) dobj ).writeObject( outs );
            }
            else {
                outs.writeByte( (int) PRIMITIVE_ID );
                ( (Primitive) dobj ).writeObject( outs );
            }
        }

        haveObjectsBeenSaved = true;
    }

    public BufForDrawables( MixedDataInput ins, final Map categorymap )
    throws java.io.IOException
    {
        this( false );
        this.readObject( ins, categorymap );
    }

    public void readObject( MixedDataInput ins, final Map categorymap )
    throws java.io.IOException
    {
        Primitive  prime;
        Composite  cmplx;
        byte       dobj_type;
        int        Nobjs, idx;

        super.readObject( ins );   // BufForObjects.readObject( ins )
            
        // assume buf4nestless contains only primitives, e.g. arrow/event
        Nobjs = ins.readInt();
        buf4nestless = new ArrayList( Nobjs );
        for ( idx = 0; idx < Nobjs; idx++ ) {
            dobj_type = ins.readByte();
            switch ( dobj_type ) {
                case PRIMITIVE_ID:
                    prime = new Primitive( ins );
                    prime.resolveCategory( categorymap );
                    buf4nestless.add( prime );
                    total_bytesize += ( prime.getByteSize() + 1 );
                    break;
                case COMPOSITE_ID:
                    cmplx = new Composite( ins );
                    cmplx.resolveCategory( categorymap );
                    buf4nestless.add( cmplx );
                    total_bytesize += ( cmplx.getByteSize() + 1 );
                    break;
                default:
                    System.err.println( "BufForDrawables: Error! "
                                      + "Unknown drawable type = "
                                      + dobj_type );
            }
        }

        // assume buf4nestable contains both primitives and composites.
        Nobjs = ins.readInt();
        buf4nestable = new ArrayList( Nobjs );
        for ( idx = 0; idx < Nobjs; idx++ ) {
            dobj_type = ins.readByte();
            switch ( dobj_type ) {
                case PRIMITIVE_ID:
                    prime = new Primitive( ins );
                    prime.resolveCategory( categorymap );
                    buf4nestable.add( prime );
                    total_bytesize += ( prime.getByteSize() + 1 );
                    break;
                case COMPOSITE_ID:
                    cmplx = new Composite( ins );
                    cmplx.resolveCategory( categorymap );
                    buf4nestable.add( cmplx );
                    total_bytesize += ( cmplx.getByteSize() + 1 );
                    break;
                default:
                    System.err.println( "BufForDrawables: Error! "
                                      + "Unknown drawable type = "
                                      + dobj_type );
            }
        }
    }

    public String toString()
    {
        Iterator  nestable_itr, nestless_itr;
        Iterator  dobjs_itr;
        int       idx;

        StringBuffer rep = new StringBuffer( "    BufForDrawables{ " );
        rep.append( super.toString() /* BufForObjects */ );
        rep.append( " }\n" );

        nestable_itr  = new IteratorOfForeDrawables( buf4nestable, this );
        nestless_itr  = new IteratorOfForeDrawables( buf4nestless, this );
        dobjs_itr     = new IteratorOfAllDrawables( nestable_itr,
                                                    nestless_itr,
                                                    buf4dobj_order );
        for ( idx = 1; dobjs_itr.hasNext(); idx++ )
            rep.append( idx + ": " + dobjs_itr.next() + "\n" );

        return rep.toString();
    }
}

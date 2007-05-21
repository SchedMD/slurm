/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Map;
import java.util.HashMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Collections;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.drawable.TimeBoundingBox;
import base.drawable.Topology;
import base.drawable.Category;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Shadow;

public class BufForShadows extends BufForObjects
{
    private static final int  INIT_BYTESIZE = BufForObjects.BYTESIZE
                                            + 4  /* buf4nestable.size() */
                                            + 4  /* buf4nestless.size() */ ;

    private Map               buf4shadows;   // For Output API
    private Map               shadowdefs_map;

    /*
        nestable = drawables that can be nested nicely, like state-like object
        nestless = drawables that need no nesting order, e.g. arrow/event
    */
    private List              buf4nestable;   /* state and composite */
    private List              buf4nestless;   /* arrow/event */
    private Drawable.Order    buf4dobj_order; /* buf4nestxxx's storage order */

    /*  
        isOutputBuf = true  when BufForShadows is used in Output API
        isOutputBuf = false when BufForShadows is used in Input API
    */
    private boolean           isOutputBuf;
    private boolean           haveObjectsBeenSaved;
    private int               total_bytesize; // bytesize for the disk footprint

    public BufForShadows( boolean isForOutput )
    {
        super();
        isOutputBuf          = isForOutput;
        // TRACE-API passes drawables in Drawable.INCRE_FINALTIME_ORDER.
        // At writeObject(), drawables are saved in INCRE_STARTTIME_ORDER.
        // At readObject(), drawables are read/stored in INCRE_STARTTIME_ORDER.
        if ( isOutputBuf ) {
            buf4shadows        = new HashMap();
            buf4nestable       = new ArrayList();
            buf4nestless       = new ArrayList();
            buf4dobj_order     = Drawable.INCRE_FINALTIME_ORDER;
        }
        else {
            buf4shadows        = null;   // Not used in Input API
            buf4nestable       = null;
            buf4nestless       = null;
            buf4dobj_order     = Drawable.INCRE_STARTTIME_ORDER;
        }

        shadowdefs_map       = null;
        haveObjectsBeenSaved = false;
        total_bytesize       = INIT_BYTESIZE;
    }

    public void setMapOfTopologyToShadowDef( Map in_shadefs )
    {
        shadowdefs_map = in_shadefs;
    }

    public int getByteSize()
    {
        return total_bytesize;
    }

    // SLOG-2 Output API
    public void add( final Primitive prime )
    {
        List      key;
        Topology  topo;
        Category  shadowdef;

        key = new ArrayList();
        topo = prime.getCategory().getTopology();
        key.add( topo );
        key.addAll( prime.getListOfVertexLineIDs() );
        Shadow sobj = (Shadow) buf4shadows.get( key );
        if ( sobj == null ) {
            shadowdef = (Category) shadowdefs_map.get( topo );
            shadowdef.setUsed( true );
            sobj = new Shadow( shadowdef, prime );
            buf4shadows.put( key, sobj );
            total_bytesize += sobj.getByteSize();
        }
        else {
            total_bytesize -= sobj.getByteSize();
            sobj.mergeWithPrimitive( prime );
            total_bytesize += sobj.getByteSize();
        }
    }

    // SLOG-2 Output API
    public void empty()
    {
        if ( haveObjectsBeenSaved ) {
            buf4nestable.clear();
            buf4nestless.clear();
            buf4dobj_order       = Drawable.INCRE_FINALTIME_ORDER;
            buf4shadows.clear();
            haveObjectsBeenSaved = false;
            total_bytesize       = INIT_BYTESIZE;
        }
    }

    // SLOG-2 Output API : the argument "buf" is childnode's BufForShadows
    public void mergeWith( final BufForShadows buf )
    {
        Map.Entry  key_sobj;
        List       key;
        Shadow     sobj;
        Iterator entries = buf.buf4shadows.entrySet().iterator();
        while ( entries.hasNext() ) {
            key_sobj = (Map.Entry) entries.next();
            key      = (List) key_sobj.getKey();
            sobj     = (Shadow) this.buf4shadows.get( key );
            if ( sobj == null ) {
                // A NEW copy of Shadow is needed here, otherwise
                // buf.buf4shadows[] could be modified by
                // this.buf4shadows[] __later__ in the program
                sobj = new Shadow( (Shadow) key_sobj.getValue() );
                this.buf4shadows.put( key, sobj );
                total_bytesize += sobj.getByteSize();
            }
            else {
                total_bytesize -= sobj.getByteSize();
                sobj.mergeWithShadow( (Shadow) key_sobj.getValue() );
                total_bytesize += sobj.getByteSize();
            }
        }
    }

    // SLOG-2 Output API
    public void initializeMapOfCategoryWeights()
    {
        Iterator  sobjs_itr;

        sobjs_itr = buf4shadows.values().iterator();
        while ( sobjs_itr.hasNext() )
            ( (Shadow) sobjs_itr.next() ).initializeMapOfCategoryWeights();
    }

    // SLOG-2 Output API
    public void finalizeMapOfCategoryWeights()
    {
        Iterator  sobjs_itr;

        sobjs_itr = buf4shadows.values().iterator();
        while ( sobjs_itr.hasNext() )
            ( (Shadow) sobjs_itr.next() ).finalizeMapOfCategoryWeights();
    }

    public int getNumOfPrimitives()
    {
        if ( isOutputBuf )
            return buf4shadows.size();
        else
            return buf4nestable.size() + buf4nestless.size();
    }

    public int getNumOfDrawables()
    {
        return this.getNumOfPrimitives();
    }

    private long getNumOfRealObjects()
    {
        Iterator objs_itr;
        long     Nrobjs = 0;
        if ( isOutputBuf ) {
            objs_itr = buf4shadows.values().iterator();
            while ( objs_itr.hasNext() )
                Nrobjs += ( (Shadow) objs_itr.next() ).getNumOfRealObjects();
        }
        else {
            objs_itr = buf4nestable.iterator();
            while ( objs_itr.hasNext() )
                Nrobjs += ( (Shadow) objs_itr.next() ).getNumOfRealObjects();
            objs_itr = buf4nestless.iterator();
            while ( objs_itr.hasNext() )
                Nrobjs += ( (Shadow) objs_itr.next() ).getNumOfRealObjects();
        }
        return Nrobjs;
    }

    // Iterator of Nestable Drawables in Increasing StartTime order
    public Iterator nestableForeIterator( final TimeBoundingBox tframe,
                                                boolean         isComposite )
    {
        // if ( ! isOutputBuf )
        return new IteratorOfForeDrawables( buf4nestable, tframe );
    }

    // Iterator of Nestless Drawables in Increasing StartTime order
    public Iterator nestlessForeIterator( final TimeBoundingBox tframe )
    {
        // if ( ! isOutputBuf )
        return new IteratorOfForeDrawables( buf4nestless, tframe );
    }

    // Iterator of Nestable Drawables in Decreasing StartTime order
    public Iterator nestableBackIterator( final TimeBoundingBox tframe,
                                                boolean         isComposite )
    {
        // if ( ! isOutputBuf )
        return new IteratorOfBackDrawables( buf4nestable, tframe );
    }

    // Iterator of Nestless Drawables in Decreasing StartTime order
    public Iterator nestlessBackIterator( final TimeBoundingBox tframe )
    {
        // if ( ! isOutputBuf )
        return new IteratorOfBackDrawables( buf4nestless, tframe );
    }

    public LineIDMap getIdentityLineIDMap()
    {
        // if ( isOutputBuf )
        return super.toIdentityLineIDMap( buf4shadows.values() );
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

    // For SLOG-2 Output API
    public void summarizeCategories()
    {
        Iterator  sobjs_itr;
        Shadow    sobj;

        sobjs_itr = buf4shadows.values().iterator();
        while ( sobjs_itr.hasNext() ) {
            sobj = (Shadow) sobjs_itr.next();
            sobj.summarizeCategories( super.getDuration() );
        }
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        Iterator  sobjs_itr;
        Shadow    sobj;
        int       Nobjs;

        super.writeObject( outs ); // BufForObjects.writeObject( outs )

        Iterator objs_itr = buf4shadows.values().iterator();
        while ( objs_itr.hasNext() ) {
            sobj = (Shadow) objs_itr.next();
            if ( sobj.getCategory().getTopology().isState() )
                buf4nestable.add( sobj );
            else
                buf4nestless.add( sobj );
        }
        this.reorderDrawables( Drawable.INCRE_STARTTIME_ORDER );

        Nobjs  = buf4nestless.size();
        outs.writeInt( Nobjs );
        sobjs_itr = buf4nestless.iterator();
        while ( sobjs_itr.hasNext() )
            ( (Shadow) sobjs_itr.next() ).writeObject( outs );

        Nobjs  = buf4nestable.size();
        outs.writeInt( Nobjs );
        sobjs_itr = buf4nestable.iterator();
        while ( sobjs_itr.hasNext() )
            ( (Shadow) sobjs_itr.next() ).writeObject( outs );

        haveObjectsBeenSaved = true;
    }

/*
    public BufForShadows( MixedDataInput ins )
    throws java.io.IOException
    {
        this( false );
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        Shadow  sobj;
        int     Nobjs, idx;;

        super.readObject( ins ); // BufForObjects.readObject( ins )

        Nobjs = ins.readInt();
        buf4nestless = new ArrayList( Nobjs );
        for ( idx = 0; idx < Nobjs; idx++ ) {
            sobj  = new Shadow( ins );
            buf4nestless.add( sobj );
            total_bytesize += sobj.getByteSize();
        }

        Nobjs = ins.readInt();
        buf4nestable = new ArrayList( Nobjs );
        for ( idx = 0; idx < Nobjs; idx++ ) {
            sobj  = new Shadow( ins );
            buf4nestable.add( sobj );
            total_bytesize += sobj.getByteSize();
        }
    }
*/

    public BufForShadows( MixedDataInput ins, final Map categorymap )
    throws java.io.IOException
    {
        this( false );
        this.readObject( ins, categorymap );
    }

    public void readObject( MixedDataInput ins, final Map categorymap )
    throws java.io.IOException
    {
        Shadow  sobj;
        int     Nobjs, idx;;

        super.readObject( ins ); // BufForObjects.readObject( ins )

        Nobjs = ins.readInt();
        buf4nestless = new ArrayList( Nobjs );
        for ( idx = 0; idx < Nobjs; idx++ ) {
            sobj  = new Shadow( ins );
            sobj.resolveCategory( categorymap );
            buf4nestless.add( sobj );
            total_bytesize += sobj.getByteSize();
        }

        Nobjs = ins.readInt();
        buf4nestable = new ArrayList( Nobjs );
        for ( idx = 0; idx < Nobjs; idx++ ) {
            sobj  = new Shadow( ins );
            sobj.resolveCategory( categorymap );
            buf4nestable.add( sobj );
            total_bytesize += sobj.getByteSize();
        }
    }

    public String toString()
    {
        Iterator  nestable_itr, nestless_itr;
        Iterator  sobjs_itr;
        int       idx;

        StringBuffer rep = new StringBuffer( "    BufForShadows{ " );
        rep.append( super.toString() /* BufForObjects */ ); 
        rep.append( " Nrobjs=" + this.getNumOfRealObjects() );
        rep.append( " }\n" );

        if ( isOutputBuf )
            sobjs_itr = buf4shadows.values().iterator();
        else {
            nestable_itr  = new IteratorOfForeDrawables( buf4nestable, this );
            nestless_itr  = new IteratorOfForeDrawables( buf4nestless, this );
            sobjs_itr     = new IteratorOfAllDrawables( nestable_itr,
                                                        nestless_itr,
                                                        buf4dobj_order );
        }
        for ( idx = 1; sobjs_itr.hasNext(); idx++ )
            rep.append( idx + ": " + sobjs_itr.next() + "\n" );

        return rep.toString();
    }
}

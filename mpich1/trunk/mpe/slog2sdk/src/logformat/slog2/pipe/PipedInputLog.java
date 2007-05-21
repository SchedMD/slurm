/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.pipe;

import java.util.Iterator;

import base.drawable.TimeBoundingBox;
import base.drawable.Kind;
import base.drawable.Topology;
import base.drawable.Category;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Composite;
import base.drawable.YCoordMap;
import base.drawable.InputAPI;
import logformat.slog2.LineIDMap;
import logformat.slog2.CategoryMap;
import logformat.slog2.LineIDMapList;
import logformat.slog2.TreeDir;
import logformat.slog2.TreeDirValue;
import logformat.slog2.input.InputLog;

public class PipedInputLog extends InputLog
                           implements InputAPI
{

    private int                   next_kind_idx;
    private TopologyIterator      itr_topo;
    private CategoryIterator      itr_objdef;
    private LineIDMapIterator     itr_lineIDmap;
    private DrawableIterator      itr_dobj;

    public PipedInputLog( String pathname )
    {
        super( pathname );
        itr_topo        = null;
        itr_objdef      = null;
        itr_lineIDmap   = null;
        itr_dobj        = null;
    }

    public void initialize()
    {
        super.initialize();
        itr_topo = new TopologyIterator( Kind.TOPOLOGY_ID );
    }

    public Kind peekNextKind()
    {
        switch ( next_kind_idx ) {
            case Kind.TOPOLOGY_ID :
                if ( itr_topo.hasNext() )
                    return Kind.TOPOLOGY;
                itr_objdef  = new CategoryIterator( Kind.CATEGORY_ID );
            case Kind.CATEGORY_ID :
                if ( itr_objdef.hasNext() )
                    return Kind.CATEGORY;
                itr_lineIDmap  = new LineIDMapIterator( Kind.YCOORDMAP_ID );
            case Kind.YCOORDMAP_ID :
                if ( itr_lineIDmap.hasNext() )
                    return Kind.YCOORDMAP;
                itr_dobj  = new DrawableIterator( Kind.COMPOSITE_ID );
            case Kind.COMPOSITE_ID :
            case Kind.PRIMITIVE_ID :
                if ( itr_dobj.hasNext() ) {
                    if ( itr_dobj.isNextComposite() )
                        return Kind.COMPOSITE;
                    else
                        return Kind.PRIMITIVE;
                }
            case Kind.EOF_ID:
                return Kind.EOF;
            default:
                System.err.println( "PipedInputLog.peekNextKind(): Error!\n"
                                  + "\tUnknown Kind index: " + next_kind_idx );
                break;
        }
        return null;
    }

    public Topology getNextTopology()
    {
        return (Topology) itr_topo.next();
    }

    // getNextCategory() is called after peekNextKind() returns Kind.CATEGORY
    public Category getNextCategory()
    {
        return (Category) itr_objdef.next();
    }

    // getNextYCoordMap() is called after peekNextKind() returns Kind.YCOORDMAP
    public YCoordMap getNextYCoordMap()
    {
        return ( (LineIDMap) itr_lineIDmap.next() ).toYCoordMap();
    }

    // getNextPrimitive() is called after peekNextKind() returns Kind.PRIMITIVE
    public Primitive getNextPrimitive()
    {
        return (Primitive) itr_dobj.next();
    }

    // getNextComposite() is called after peekNextKind() returns Kind.COMPOSITE
    public Composite getNextComposite()
    {
        return (Composite) itr_dobj.next();
    }

    public void close()
    {
        super.close();
    }




    private class TopologyIterator implements Iterator
    {
        private int  num_topology_returned;

        public TopologyIterator( int kind_idx )
        {
             PipedInputLog.this.next_kind_idx  = kind_idx;
             num_topology_returned  = 0;
        }

        public boolean hasNext()
        {
             return num_topology_returned < 3;
        }

        public Object next()
        {
            switch ( num_topology_returned ) {
                case 0:
                    num_topology_returned = 1;
                    return Topology.EVENT;
                case 1:
                    num_topology_returned = 2;
                    return Topology.STATE;
                case 2:
                    num_topology_returned = 3;
                    return Topology.ARROW;
                default:
                    System.err.println( "All Topologies have been returned." );
            }
            return null;
        }
    
        public void remove() {}
    }


    private class CategoryIterator implements Iterator 
    {
        private Iterator     objdef_itr;
        private Category     next_objdef;

        public CategoryIterator( int kind_idx )
        {
            PipedInputLog.this.next_kind_idx  = kind_idx;

            CategoryMap  objdefs;
            objdefs     = PipedInputLog.super.getCategoryMap();
            objdef_itr  = objdefs.values().iterator();
            if ( objdef_itr.hasNext() )
                next_objdef = (Category) objdef_itr.next();
            else
                next_objdef = null;
        }

        public boolean hasNext()
        {
            return  next_objdef != null;
        }

        public Object next()
        {
            Category  loosen_objdef  = next_objdef;
            if ( objdef_itr.hasNext() )
                next_objdef = (Category) objdef_itr.next();
            else
                next_objdef = null;
            return loosen_objdef;
        }

        public void remove() {}
    }


    private class LineIDMapIterator implements Iterator 
    {
        private Iterator      lineIDmap_itr;
        private LineIDMap     next_lineIDmap;

        public LineIDMapIterator( int kind_idx )
        {
            PipedInputLog.this.next_kind_idx  = kind_idx;
    
            LineIDMapList  lineIDmaps;
            lineIDmaps     = PipedInputLog.super.getLineIDMapList();
            lineIDmap_itr  = lineIDmaps.iterator();
            if ( lineIDmap_itr.hasNext() )
                next_lineIDmap = (LineIDMap) lineIDmap_itr.next();
            else
                next_lineIDmap = null;
        }
    
        public boolean hasNext()
        {
            return  next_lineIDmap != null;
        }
    
        public Object next()
        {
            LineIDMap  loosen_lineIDmap  = next_lineIDmap;
            if ( lineIDmap_itr.hasNext() )
                next_lineIDmap = (LineIDMap) lineIDmap_itr.next();
            else
                next_lineIDmap = null;
            return loosen_lineIDmap;
        }

        public void remove() {}
    }


    private class DrawableIterator implements Iterator
    {
        private final Drawable.Order  TRACE_ORDER
                                      = Drawable.INCRE_FINALTIME_ORDER;
        private Iterator   dobj_itr;
        private Drawable   next_dobj;

        public DrawableIterator( int kind_idx )
        {
            PipedInputLog.this.next_kind_idx  = kind_idx;
    
            TimeBoundingBox  timeframe;
            TreeDir          treedir;
            TreeDirValue     root_dir;
            
            treedir = PipedInputLog.super.getTreeDir();
            // System.out.println( treedir );
    
            root_dir  = (TreeDirValue) treedir.get( treedir.firstKey() );
            timeframe = new TimeBoundingBox( root_dir.getTimeBoundingBox() );
            dobj_itr  = PipedInputLog.super.iteratorOfRealDrawables( timeframe,
                                                      TRACE_ORDER,
                                                      InputLog.ITERATE_ALL );
            if ( dobj_itr.hasNext() )
                next_dobj = (Drawable) dobj_itr.next();
            else
                next_dobj = null;
        }
    
        public boolean hasNext()
        {
            return  next_dobj != null;
        }

        public Object next()
        {
            Drawable  loosen_dobj  = next_dobj;
            if ( dobj_itr.hasNext() )
                next_dobj = (Drawable) dobj_itr.next();
            else
                next_dobj = null;
            return loosen_dobj;
        }

        public boolean isNextComposite()
        {
            return (next_dobj instanceof Composite);
        }

        public void remove() {}
    }
}

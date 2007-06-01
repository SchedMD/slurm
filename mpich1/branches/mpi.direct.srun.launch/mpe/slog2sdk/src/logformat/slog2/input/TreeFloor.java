/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.input;

import java.util.SortedMap;
import java.util.TreeMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.ListIterator;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import logformat.slog2.*;

/*
   TreeFloor is a SortedSet( or SortedMap ) of TreeNode
             whose TreeNodeID.depth are the same.
*/
public class TreeFloor extends TreeMap
{
    private short              depth;
    private TimeBoundingBox    timebounds;
    private boolean            isIncreTimeOrdered;

    public TreeFloor( short in_depth, final BufForObjects.Order buf4objs_order )
    {
        super( buf4objs_order );  //  TreeMap( java.util.Comparator )
        depth               = in_depth;
        timebounds          = new TimeBoundingBox();
        isIncreTimeOrdered  = buf4objs_order.isIncreasingIndexOrdered();
    }

    public short getDepth()
    {
        return depth;
    }

    public TimeBoundingBox earliestTimeBounds()
    {
        if ( isIncreTimeOrdered )
            return (TimeBoundingBox) super.firstKey();
        else
            return (TimeBoundingBox) super.lastKey();
    }

    public TimeBoundingBox latestTimeBounds()
    {
        if ( isIncreTimeOrdered )
            return (TimeBoundingBox) super.lastKey();
        else
            return (TimeBoundingBox) super.firstKey();
    }

    public TimeBoundingBox getTimeBounds()
    {
        timebounds.setEarliestTime( earliestTimeBounds().getEarliestTime() );
        timebounds.setLatestTime( latestTimeBounds().getLatestTime() );
        if ( ! timebounds.isTimeOrdered() ) {
            System.out.println( "slog2.input.TreeFloor.getTimeBounds() "
                              + "returns wrong " + timebounds );
        }
        return timebounds;
    }

    public boolean coversBarely( final TimeBoundingBox  tframe )
    {
        return    earliestTimeBounds().contains( tframe.getEarliestTime() )
               && latestTimeBounds().contains( tframe.getLatestTime() );
    }

    public boolean covers( final TimeBoundingBox  tframe )
    {
        this.getTimeBounds();
        return timebounds.covers( tframe );
    }

    public boolean overlaps( final TimeBoundingBox  tframe )
    {
        this.getTimeBounds();
        return timebounds.overlaps( tframe );
    }

    public boolean disjoints( final TimeBoundingBox  tframe )
    {
        this.getTimeBounds();
        return timebounds.disjoints( tframe );
    }

    public void pruneToBarelyCovering( final TimeBoundingBox  tframe )
    {
        if ( this.covers( tframe ) ) {
            double starttime = tframe.getEarliestTime(); 
            double finaltime = tframe.getLatestTime();
            while ( ! this.coversBarely( tframe ) ) {
                if ( ! earliestTimeBounds().contains( starttime ) )
                    super.remove( super.firstKey() );
                if ( ! latestTimeBounds().contains( finaltime ) )
                    super.remove( super.lastKey() );
            }
        }
    }

    public Iterator iteratorOfDrawables( final TimeBoundingBox  tframe,
                                         final Drawable.Order   dobj_order,
                                               boolean          isComposite,
                                               boolean          isNestable )
    {
        return new ItrOfDrawables( tframe, dobj_order,
                                   isComposite, isNestable );
    }

    public Iterator iteratorOfShadows( final TimeBoundingBox  tframe,
                                       final Drawable.Order   dobj_order,
                                             boolean          isNestable )
    {
        return new ItrOfShadows( tframe, dobj_order, isNestable );
    }

    public String toStubString()
    {
        StringBuffer rep = new StringBuffer();
        Iterator itr = this.keySet().iterator();
        while ( itr.hasNext() ) {
            BufStub nodestub = new BufStub( (BufForObjects) itr.next() );
            rep.append( nodestub.toString() + "\n" );
        }
        return rep.toString();
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer();
        Iterator itr = this.values().iterator();
        while ( itr.hasNext() )
            rep.append( itr.next().toString() + "\n" );
        return rep.toString();
    }



    private class ItrOfDrawables extends IteratorOfGroupObjects
    {
        private              Drawable.Order   dobj_order;
        private              boolean          isComposite;
        private              boolean          isNestable;

        private              ListIterator     nodes_itr;
        private              boolean          isSameDir;

        public ItrOfDrawables( final TimeBoundingBox  tframe,
                               final Drawable.Order   in_dobj_order,
                                     boolean          in_isComposite,
                                     boolean          in_isNestable )
        {
            super( tframe );
            dobj_order  = in_dobj_order;
            isNestable  = in_isNestable;
            isComposite = in_isComposite;

            isSameDir   = (  dobj_order.isIncreasingTimeOrdered()
                          == TreeFloor.this.isIncreTimeOrdered );

            List nodes  = new ArrayList( TreeFloor.super.values() );
            if ( isSameDir )
                nodes_itr   = nodes.listIterator( 0 );
            else
                nodes_itr   = nodes.listIterator( nodes.size() );
            super.setObjGrpItr( this.nextObjGrpItr( tframe ) );
        }

        protected Iterator nextObjGrpItr( final TimeBoundingBox tframe )
        {
            TreeNode         node;

            // nodes_itr is guaranteed to be NOT null by TreeMap.values(),
            // No need to check for nodes_itr != null.
            if ( isSameDir) {
                while ( nodes_itr.hasNext() ) {
                    node       = (TreeNode) nodes_itr.next();
                    if ( node.overlaps( tframe ) )
                        return node.iteratorOfDrawables( tframe, dobj_order,
                                                         isComposite,
                                                         isNestable );
                }
            }
            else {
                while ( nodes_itr.hasPrevious() ) {
                    node       = (TreeNode) nodes_itr.previous();
                    if ( node.overlaps( tframe ) )
                        return node.iteratorOfDrawables( tframe, dobj_order,
                                                         isComposite,
                                                         isNestable );
                }
            }
            return null;
        }
    }   // private class ItrOfDrawables

    private class ItrOfShadows extends IteratorOfGroupObjects
    {
        private              Drawable.Order   dobj_order;
        private              boolean          isNestable;

        private              ListIterator     nodes_itr;
        private              boolean          isSameDir;

        public ItrOfShadows( final TimeBoundingBox  tframe,
                             final Drawable.Order   in_dobj_order,
                                   boolean          in_isNestable )
        {
            super( tframe );
            dobj_order  = in_dobj_order;
            isNestable  = in_isNestable;

            isSameDir   = (  dobj_order.isIncreasingTimeOrdered()
                          == TreeFloor.this.isIncreTimeOrdered );

            List nodes  = new ArrayList( TreeFloor.super.values() );
            if ( isSameDir )
                nodes_itr   = nodes.listIterator( 0 );
            else
                nodes_itr   = nodes.listIterator( nodes.size() );
            super.setObjGrpItr( this.nextObjGrpItr( tframe ) );
        }

        protected Iterator nextObjGrpItr( final TimeBoundingBox tframe )
        {
            TreeNode         node;

            // nodes_itr is guaranteed to be NOT null by TreeMap.values(),
            // No need to check for nodes_itr != null.
            if ( isSameDir) {
                while ( nodes_itr.hasNext() ) {
                    node       = (TreeNode) nodes_itr.next();
                    if ( node.overlaps( tframe ) )
                        return node.iteratorOfShadows( tframe, dobj_order,
                                                       isNestable );
                }
            }
            else {
                while ( nodes_itr.hasPrevious() ) {
                    node       = (TreeNode) nodes_itr.previous();
                    if ( node.overlaps( tframe ) )
                        return node.iteratorOfShadows( tframe, dobj_order,
                                                       isNestable );
                }
            }
            return null;
        }
    }   // private class ItrOfShadows

}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.util.Iterator;
import java.util.Map;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import base.drawable.Shadow;
import base.drawable.Category;
import base.statistics.BufForTimeAveBoxes;
import logformat.slog2.input.TreeTrunk;
import viewer.zoomable.YaxisTree;

public class SearchTreeTrunk
{
    private static final Drawable.Order       INCRE_STARTTIME_ORDER
                                              = Drawable.INCRE_STARTTIME_ORDER;
    private static final Drawable.Order       DECRE_STARTTIME_ORDER
                                              = Drawable.DECRE_STARTTIME_ORDER;
    private static final boolean              IS_NESTABLE         = true;

    private              TreeTrunk            treetrunk;
    private              boolean              isConnectedComposite;
    private              SearchCriteria       criteria;
    private              Drawable             last_found_dobj;

    public SearchTreeTrunk( TreeTrunk  treebody, final YaxisTree y_tree,
                            boolean    isComposite )
    {
        treetrunk             = treebody;
        criteria              = new SearchCriteria( y_tree );
        isConnectedComposite  = isComposite;
        last_found_dobj       = null;
    }

    // This is for a backward NEW SEARCH
    public Drawable previousDrawable( double searching_time )
    {
        Iterator  dobjs;
        Drawable  dobj;
        /*
           Use an infinite TimeBoundingBox so iteratorOfAllDrawables() returns
           all drawables in the memory disregarding the treefloor's timebounds
        */
        dobjs = treetrunk.iteratorOfAllDrawables( TimeBoundingBox.ALL_TIMES,
                                                  DECRE_STARTTIME_ORDER,
                                                  isConnectedComposite,
                                                  IS_NESTABLE );
        criteria.initMatch();
        while ( dobjs.hasNext() ) {
            dobj    = (Drawable) dobjs.next();
            if (    dobj.getCategory().isVisiblySearchable()
                 && dobj.getEarliestTime() <= searching_time
                 && dobj.containSearchable()
                 && criteria.isMatched( dobj ) ) { 
                last_found_dobj = dobj;
                return last_found_dobj;
            }
        }
        last_found_dobj = null;
        return null;
    }

    // This is for a backward CONTINUING SEARCH
    public Drawable previousDrawable()
    {
        Iterator  dobjs;
        Drawable  dobj;

        if ( last_found_dobj == null ) {
            System.err.println( "SearchTreeTrunk.previousDrawable(): "
                              + "Unexpected error, last_found_dobj == null" );
            return null;
        }
        /*
           Use an infinite TimeBoundingBox so iteratorOfAllDrawables() returns
           all drawables in the memory disregarding the treefloor's timebounds
        */
        dobjs = treetrunk.iteratorOfAllDrawables( TimeBoundingBox.ALL_TIMES,
                                                  DECRE_STARTTIME_ORDER,
                                                  isConnectedComposite,
                                                  IS_NESTABLE );
        criteria.initMatch();
        while ( dobjs.hasNext() ) {
            dobj    = (Drawable) dobjs.next();
            if (    dobj.getCategory().isVisiblySearchable()
                 && DECRE_STARTTIME_ORDER.compare( dobj, last_found_dobj ) > 0
                 && dobj.containSearchable()
                 && criteria.isMatched( dobj ) ) {
                last_found_dobj = dobj;
                return last_found_dobj;
            }
        }
        last_found_dobj = null;
        return null;
    }

    // This is for a forward NEW SEARCH
    public Drawable nextDrawable( double searching_time )
    {
        Iterator  dobjs;
        Drawable  dobj;
        /*
           Use an infinite TimeBoundingBox so iteratorOfAllDrawables() returns
           all drawables in the memory disregarding the treefloor's timebounds
        */
        dobjs = treetrunk.iteratorOfAllDrawables( TimeBoundingBox.ALL_TIMES,
                                                  INCRE_STARTTIME_ORDER,
                                                  isConnectedComposite,
                                                  IS_NESTABLE );
        criteria.initMatch();
        while ( dobjs.hasNext() ) {
            dobj    = (Drawable) dobjs.next();
            if (    dobj.getCategory().isVisiblySearchable()
                 && dobj.getEarliestTime() >= searching_time
                 && dobj.containSearchable()
                 && criteria.isMatched( dobj ) ) {
                last_found_dobj = dobj;
                return last_found_dobj;
            }
        }
        last_found_dobj = null;
        return null;
    }

    // This is for a forward CONTINUING SEARCH
    public Drawable nextDrawable()
    {
        Iterator  dobjs;
        Drawable  dobj;

        if ( last_found_dobj == null ) {
            System.err.println( "SearchTreeTrunk.nextDrawable(): "
                              + "Unexpected error, last_found_dobj == null" );
            return null;
        }
        /*
           Use an infinite TimeBoundingBox so iteratorOfAllDrawables() returns
           all drawables in the memory disregarding the treefloor's timebounds
        */
        dobjs = treetrunk.iteratorOfAllDrawables( TimeBoundingBox.ALL_TIMES,
                                                  INCRE_STARTTIME_ORDER,
                                                  isConnectedComposite,
                                                  IS_NESTABLE );
        criteria.initMatch();
        while ( dobjs.hasNext() ) {
            dobj    = (Drawable) dobjs.next();
            if (    dobj.getCategory().isVisiblySearchable()
                 && INCRE_STARTTIME_ORDER.compare( dobj, last_found_dobj ) > 0
                 && dobj.containSearchable()
                 && criteria.isMatched( dobj ) ) {
                last_found_dobj = dobj;
                return last_found_dobj;
            }
        }
        last_found_dobj = null;
        return null;
    }

    public BufForTimeAveBoxes
    createBufForTimeAveBoxes( final TimeBoundingBox  timebox )
    {
        BufForTimeAveBoxes  buf2statboxes;
        Iterator            dobjs, sobjs;
        Drawable            dobj;
        Shadow              sobj;

        buf2statboxes   = new BufForTimeAveBoxes( timebox );
        criteria.initMatch();

        // Merge Nestable Shadows
        sobjs = treetrunk.iteratorOfLowestFloorShadows( timebox,
                                                        INCRE_STARTTIME_ORDER,
                                                        IS_NESTABLE );
        while ( sobjs.hasNext() ) {
            sobj = (Shadow) sobjs.next();
            if (    sobj.getCategory().isVisiblySearchable()
                 && sobj.containSearchable()
                 && criteria.isMatched( sobj ) ) {
                buf2statboxes.mergeWithNestable( sobj );
            }
        }

        // Merge Nestable Real Drawables
        dobjs = treetrunk.iteratorOfRealDrawables( timebox,
                                                   INCRE_STARTTIME_ORDER,
                                                   isConnectedComposite,
                                                   IS_NESTABLE );
        while ( dobjs.hasNext() ) {
            dobj = (Drawable) dobjs.next();
            if (    dobj.getCategory().isVisiblySearchable()
                 && dobj.containSearchable()
                 && criteria.isMatched( dobj ) ) {
                buf2statboxes.mergeWithNestable( dobj );
            }
        }

        // Compute ExclusiveDurationRatio of CategoryWeights in buf2statboxes
        buf2statboxes.setNestingExclusion();

        // Merge Nestless Real Drawables
        dobjs = treetrunk.iteratorOfRealDrawables( timebox,
                                                   INCRE_STARTTIME_ORDER,
                                                   isConnectedComposite,
                                                   !IS_NESTABLE );
        while ( dobjs.hasNext() ) {
            dobj = (Drawable) dobjs.next();
            if (    dobj.getCategory().isVisiblySearchable()
                 && dobj.containSearchable()
                 && criteria.isMatched( dobj ) ) {
                buf2statboxes.mergeWithNestless( dobj );
            }
        }

        // Merge Nestless Shadows
        sobjs = treetrunk.iteratorOfLowestFloorShadows( timebox,
                                                        INCRE_STARTTIME_ORDER,
                                                        !IS_NESTABLE );
        while ( sobjs.hasNext() ) {
            sobj = (Shadow) sobjs.next();
            if (    sobj.getCategory().isVisiblySearchable()
                 && sobj.containSearchable()
                 && criteria.isMatched( sobj ) ) {
                buf2statboxes.mergeWithNestless( sobj );
            }
        }

        return buf2statboxes;
    }
}

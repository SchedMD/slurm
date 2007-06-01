/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import java.util.Map;
import java.util.HashMap;
import java.util.SortedSet;
import java.util.TreeSet;
import java.util.List;
import java.util.ArrayList;
import java.util.Stack;
import java.util.Arrays;
import java.util.Iterator;

import base.drawable.TimeBoundingBox;
import base.drawable.Category;
import base.drawable.CategoryWeight;
import base.drawable.Drawable;
import base.drawable.Composite;
import base.drawable.Primitive;
import base.drawable.Shadow;

public class TimeAveBox extends TimeBoundingBox
{
    private              Map                 map_type2twgf;
    private              List                list_nestables;
    private              SortedSet           set_timeblocks;
    private              CategoryTimeBox[]   typebox_ary;
    private              double              num_real_objs;
    private              double              box_duration;

    private              TimeBoundingBox     curr_timebox;

    public TimeAveBox( final TimeBoundingBox  timebox,
                             boolean          isNestable )
    {
        super( timebox );
        map_type2twgf   = new HashMap();
        box_duration    = super.getDuration();
        num_real_objs   = 0.0d;
        typebox_ary     = null;
        curr_timebox    = null;

        if ( isNestable ) {
            list_nestables = new ArrayList();
            set_timeblocks = new TreeSet(TimeBoundingBox.INCRE_STARTTIME_ORDER);
        }
        else {
            list_nestables = null;
            set_timeblocks = null;
        }
    }

    public TimeAveBox( final TimeAveBox  avebox )
    {
        this( avebox, avebox.list_nestables != null );
        this.mergeWithTimeAveBox( avebox );
    }

    public void mergeWithReal( final Drawable  dobj )
    {
        Category          type;
        CategoryWeightF   twgf;
        double            overlap_duration;
        float             box_overlap_ratio;
        double            ave_num_real_objs;

        overlap_duration   = super.getIntersectionDuration( dobj );
        box_overlap_ratio  = (float) (overlap_duration / box_duration);
        ave_num_real_objs  = overlap_duration / dobj.getDuration()
                           * dobj.getNumOfPrimitives();

        type  = dobj.getCategory();
        twgf  = (CategoryWeightF) map_type2twgf.get( type );
        if ( twgf == null ) {
            twgf  = new CategoryWeightF( type, box_overlap_ratio, 0.0f,
                                         ave_num_real_objs );
            map_type2twgf.put( type, twgf );
        }
        else {
            twgf.addDrawableCount( ave_num_real_objs );
            twgf.addInclusiveRatio( box_overlap_ratio );
        }
        num_real_objs += ave_num_real_objs;

        if ( list_nestables != null )
            list_nestables.add( dobj );
    }

    public void mergeWithShadow( final Shadow  shade )
    {
        TimeBoundingBox   timeblock;
        Category          sobj_type;
        CategoryWeightF   this_twgf;
        CategoryWeight    sobj_twgt;
        CategoryWeight[]  sobj_twgts;
        double            overlap_duration;
        float             box_overlap_ratio;
        double            sobj_overlap_ratio;
        double            ave_num_real_objs;
        int               idx;

        overlap_duration   = super.getIntersectionDuration( shade );
        box_overlap_ratio  = (float) (overlap_duration / box_duration);
        sobj_overlap_ratio = overlap_duration / shade.getDuration();
        ave_num_real_objs  = sobj_overlap_ratio * shade.getNumOfRealObjects();

        sobj_twgts = shade.arrayOfCategoryWeights();
        for ( idx = sobj_twgts.length-1 ; idx >= 0 ; idx-- ) {
            sobj_twgt = sobj_twgts[ idx ];
            sobj_type = sobj_twgt.getCategory();
            this_twgf = (CategoryWeightF) map_type2twgf.get( sobj_type );
            if ( this_twgf == null ) {
                // sobj_twgt's clone + rescaling ratios & num_real_objs
                this_twgf = new CategoryWeightF( sobj_twgt );
                this_twgf.rescaleAllRatios( box_overlap_ratio );
                this_twgf.rescaleDrawableCount( sobj_overlap_ratio );
                map_type2twgf.put( sobj_type, this_twgf );
            }
            else {
                this_twgf.addDrawableCount( sobj_overlap_ratio
                                          * sobj_twgt.getDrawableCount() );
                this_twgf.addAllRatios( sobj_twgt, box_overlap_ratio );
            }
        }
        num_real_objs += ave_num_real_objs;

        if ( list_nestables != null )
            set_timeblocks.add( shade );
    }

    public void mergeWithTimeAveBox( final TimeAveBox  avebox )
    {
        TimeBoundingBox   timeblock;
        Category          abox_type;
        CategoryWeightF   abox_twgf, this_twgf;
        Iterator          abox_twgfs;
        double            overlap_duration;
        float             box_overlap_ratio;
        double            abox_overlap_ratio;
        double            ave_num_real_objs;
        int               idx;

        overlap_duration   = super.getIntersectionDuration( avebox );
        box_overlap_ratio  = (float) (overlap_duration / box_duration);
        abox_overlap_ratio = overlap_duration / avebox.getDuration();
        ave_num_real_objs  = abox_overlap_ratio * avebox.num_real_objs;

        abox_twgfs = avebox.map_type2twgf.values().iterator();
        while ( abox_twgfs.hasNext() ) {
            abox_twgf = (CategoryWeightF) abox_twgfs.next();
            abox_type = abox_twgf.getCategory();
            this_twgf = (CategoryWeightF) map_type2twgf.get( abox_type );
            if ( this_twgf == null ) {
                // abox_twgf's clone + rescaling ratios & num_real_objs
                this_twgf = new CategoryWeightF( abox_twgf );
                this_twgf.rescaleAllRatios( box_overlap_ratio );
                this_twgf.rescaleDrawableCount( abox_overlap_ratio );
                map_type2twgf.put( abox_type, this_twgf );
            }
            else {
                this_twgf.addDrawableCount( abox_overlap_ratio
                                          * abox_twgf.getDrawableCount() );
                this_twgf.addAllRatios( abox_twgf, box_overlap_ratio );
            }
        }
        num_real_objs += ave_num_real_objs;

        if ( list_nestables != null )
            set_timeblocks.add( avebox );
    }

    private void patchSetOfTimeBlocks()
    {
        TimeBoundingBox  first_timeblock, last_timeblock, new_timeblock;

        new_timeblock   = new TimeBoundingBox( TimeBoundingBox.ALL_TIMES );
        first_timeblock = null;
        if ( ! set_timeblocks.isEmpty() )
            first_timeblock = (TimeBoundingBox) set_timeblocks.first();
        if (    first_timeblock != null
             && first_timeblock.contains( super.getEarliestTime() ) )
            new_timeblock.setLatestTime( first_timeblock.getEarliestTime() );
        else
            new_timeblock.setLatestTime( super.getEarliestTime() );
        set_timeblocks.add( new_timeblock );

        new_timeblock  = new TimeBoundingBox( TimeBoundingBox.ALL_TIMES );
        last_timeblock = null;
        if ( ! set_timeblocks.isEmpty() )
            last_timeblock = (TimeBoundingBox) set_timeblocks.last();
        if (    last_timeblock != null
             && last_timeblock.contains( super.getLatestTime() ) )
            new_timeblock.setEarliestTime( last_timeblock.getLatestTime() );
        else
            new_timeblock.setEarliestTime( super.getLatestTime() );
        set_timeblocks.add( new_timeblock );
    }

    //  same as Shadow.setNestingExclusion()
    private void setRealDrawableExclusion()
    {
        Object[]          timeblocks;
        Stack             nesting_stack;
        Iterator          dobjs_itr;
        Drawable          curr_dobj, stacked_dobj;

        timeblocks     = set_timeblocks.toArray();
        nesting_stack  = new Stack();

        //  Assume dobjs_itr returns in Increasing Starttime order
        dobjs_itr      = list_nestables.iterator();
        while ( dobjs_itr.hasNext() ) {
            curr_dobj  = (Drawable) dobjs_itr.next();
            curr_dobj.initExclusion( timeblocks );
            while ( ! nesting_stack.empty() ) {
                stacked_dobj = (Drawable) nesting_stack.peek();
                if ( stacked_dobj.covers( curr_dobj ) ) {
                    stacked_dobj.decrementExclusion( curr_dobj.getExclusion() );
                    break;
                }
                else
                    nesting_stack.pop();
            }
            nesting_stack.push( curr_dobj );
        }
        nesting_stack.clear();
        nesting_stack  = null;

        timeblocks     = null;
        set_timeblocks.clear();
        set_timeblocks = null;
    }

    private void adjustMapOfCategoryWeightFs()
    {
        Iterator          dobjs_itr;
        Drawable          curr_dobj;
        Category          dobj_type;
        CategoryWeightF   dobj_twgf;
        float             excl_ratio;

        dobjs_itr      = list_nestables.iterator();
        while ( dobjs_itr.hasNext() ) {
            curr_dobj  = (Drawable) dobjs_itr.next();
            excl_ratio = (float) ( curr_dobj.getExclusion() / box_duration );
            dobj_type  = curr_dobj.getCategory();
            // CategoryWeightF is guaranteed to be in map_type2twgf
            dobj_twgf  = (CategoryWeightF) map_type2twgf.get( dobj_type );
            dobj_twgf.addExclusiveRatio( excl_ratio );
        }
        list_nestables.clear();
        // Don't set list_nestables=null so list_nestables indicates Nestability
        // list_nestables = null;
    }

    public void setNestingExclusion()
    {
        this.patchSetOfTimeBlocks();
        this.setRealDrawableExclusion();
        this.adjustMapOfCategoryWeightFs();
    }

    public double getAveNumOfRealObjects()
    {
        return num_real_objs;
    }

    public void initializeCategoryTimeBoxes()
    {
        Iterator        twgfs_itr;
        CategoryWeightF twgf;
        CategoryTimeBox typebox;
        int             idx;

        if ( typebox_ary == null ) {
            typebox_ary = new CategoryTimeBox[ map_type2twgf.size() ];
            idx         = 0;
            twgfs_itr   = map_type2twgf.values().iterator();
            while ( twgfs_itr.hasNext() ) {
                twgf    = (CategoryWeightF) twgfs_itr.next();
                typebox = new CategoryTimeBox( twgf );
                typebox_ary[ idx ] = typebox;
                idx++;
            }
        }
    }

    public CategoryTimeBox[] arrayOfCategoryTimeBoxes()
    { return typebox_ary; }

    public TimeBoundingBox  getCurrentTimeBoundingBox()
    {
        if ( curr_timebox == null )
            curr_timebox = new TimeBoundingBox();
        return curr_timebox;
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( super.toString() );
        rep.append( " Nrobjs=" + (float) num_real_objs );

        if ( map_type2twgf.size() > 0 ) {
            Object[] twgfs;
            twgfs = map_type2twgf.values().toArray( new CategoryWeightF[0] );
            Arrays.sort( twgfs, CategoryWeightF.INCL_RATIO_ORDER );
            int  twgfs_length = twgfs.length;
            for ( int idx = 0; idx < twgfs_length; idx++ )
                rep.append( "\n" + twgfs[ idx ] );
            rep.append( "\n" );
        }
        return rep.toString();
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import java.awt.Graphics2D;
import java.awt.Point;
import java.awt.Color;
import java.util.Map;
import java.util.HashMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Collections;

import base.drawable.TimeBoundingBox;
import base.drawable.Topology;
import base.drawable.Category;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Shadow;
import base.drawable.CoordPixelXform;
import base.topology.SummaryState;
import base.topology.SummaryArrow;

public class BufForTimeAveBoxes extends TimeBoundingBox
{
    private Map               map_lines2nestable;   /* state and composite */
    private Map               map_lines2nestless;   /* arrow/event */
    private Map               map_rows2nestable;    /* state and composite */
    private Map               map_rows2nestless;    /* arrow/event */

    private float             init_state_height_ftr;
    private float             next_state_height_ftr;

    private boolean           drawStates;
    private boolean           drawArrows;
    

    public BufForTimeAveBoxes( final TimeBoundingBox timebox )
    {
        super( timebox );
        map_lines2nestable  = new HashMap();
        map_lines2nestless  = new HashMap();
        map_rows2nestable   = null;
        map_rows2nestless   = null;

        drawStates          = true;
        drawArrows          = true;
    }

    //  Assume dobj is Nestable
    public void mergeWithNestable( final Drawable dobj )
    {
        List        key;
        Topology    topo;
        TimeAveBox  avebox;
        Integer     lineID_start, lineID_final;

        key  = new ArrayList();
        topo = dobj.getCategory().getTopology();
        key.add( topo );
        // key.addAll( prime.getListOfVertexLineIDs() );
        lineID_start = new Integer( dobj.getStartVertex().lineID );
        lineID_final = new Integer( dobj.getFinalVertex().lineID );
        key.add( lineID_start );
        key.add( lineID_final );
        avebox = null;
        avebox = (TimeAveBox) map_lines2nestable.get( key );
        if ( avebox == null ) {
            avebox = new TimeAveBox( this, true );
            map_lines2nestable.put( key, avebox );
        }
        if ( dobj instanceof Shadow )
            avebox.mergeWithShadow( (Shadow) dobj );
        else
            avebox.mergeWithReal( dobj );
    }

    //  Assume dobj is Nestless
    public void mergeWithNestless( final Drawable dobj )
    {
        List        key;
        Topology    topo;
        TimeAveBox  avebox;
        Integer     lineID_start, lineID_final;

        key  = new ArrayList();
        topo = dobj.getCategory().getTopology();
        key.add( topo );
        // key.addAll( prime.getListOfVertexLineIDs() );
        lineID_start = new Integer( dobj.getStartVertex().lineID );
        lineID_final = new Integer( dobj.getFinalVertex().lineID );
        key.add( lineID_start );
        key.add( lineID_final );
        avebox = null;
        avebox = (TimeAveBox) map_lines2nestless.get( key );
        if ( avebox == null ) {
            avebox = new TimeAveBox( this, false );
            map_lines2nestless.put( key, avebox );
        }
        if ( dobj instanceof Shadow )
            avebox.mergeWithShadow( (Shadow) dobj );
        else
            avebox.mergeWithReal( dobj );
    }

    public void setNestingExclusion()
    {
        Iterator    avebox_itr;
        TimeAveBox  avebox;
        
        avebox_itr = map_lines2nestable.values().iterator();
        while ( avebox_itr.hasNext() ) {
            avebox = (TimeAveBox) avebox_itr.next();
            avebox.setNestingExclusion();
        }
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( super.toString() );
        
        if ( map_lines2nestable.size() > 0 ) {
            Map.Entry  entry;
            Object[]   key;
            TimeAveBox avebox;
            Iterator   entries  = map_lines2nestable.entrySet().iterator();
            while ( entries.hasNext() ) {
                entry  = (Map.Entry) entries.next();
                key    = ( (List) entry.getKey() ).toArray();
                avebox = (TimeAveBox) entry.getValue();
                rep.append( "\n" + key[0] + ": " + key[1] + ", " + key[2] );
                rep.append( "\n" + avebox );
            }
            rep.append( "\n" );
        }

        if ( map_lines2nestless.size() > 0 ) {
            Map.Entry  entry;
            Object[]   key;
            TimeAveBox avebox;
            Iterator   entries  = map_lines2nestless.entrySet().iterator();
            while ( entries.hasNext() ) {
                entry  = (Map.Entry) entries.next();
                key    = ( (List) entry.getKey() ).toArray();
                avebox = (TimeAveBox) entry.getValue();
                rep.append( "\n" + key[0] + ": " + key[1] + ", " + key[2] );
                rep.append( "\n" + avebox );
            }
            rep.append( "\n" );
        }

        return rep.toString();
    }



    /*  Drawing related API */

    private static List  getLine2RowMappedKey( final Map   map_line2row,
                                               final List  old_keylist )
    {
        List      new_keylist;
        Object[]  old_keys;

        new_keylist    = new ArrayList(); 
        old_keys       = old_keylist.toArray(); 
        new_keylist.add( old_keys[0] );  // Topology
        new_keylist.add( map_line2row.get( (Integer) old_keys[1] ) );
        new_keylist.add( map_line2row.get( (Integer) old_keys[2] ) );
        return new_keylist;
    }

    public void setDrawingStates( boolean isDrawing )
    { drawStates  = isDrawing; }

    public void setDrawingArrows( boolean isDrawing )
    { drawArrows  = isDrawing; }

    public void initializeDrawing( final Map      map_line2row,
                                   final Color    background_color,
                                         boolean  isZeroTimeOrigin,
                                         float    init_state_height,
                                         float    next_state_height )
    {
        Map.Entry          entry;
        Iterator           entry_itr, avebox_itr;
        List               lined_key, rowed_key;
        TimeAveBox         lined_avebox, rowed_avebox;

        // For Nestables, i.e. states
        map_rows2nestable  = new HashMap();
        entry_itr = map_lines2nestable.entrySet().iterator();
        while ( entry_itr.hasNext() ) {
            entry        = (Map.Entry)  entry_itr.next();
            lined_key    = (List)       entry.getKey();
            lined_avebox = (TimeAveBox) entry.getValue();
            
            rowed_key    = getLine2RowMappedKey( map_line2row, lined_key );
            rowed_avebox = (TimeAveBox) map_rows2nestable.get( rowed_key );
            if ( rowed_avebox == null ) {
                rowed_avebox = new TimeAveBox( lined_avebox );
                map_rows2nestable.put( rowed_key, rowed_avebox );
            }
            else
                rowed_avebox.mergeWithTimeAveBox( lined_avebox );
        }

        // Do setNestingExclusion() for map_rows2nestable
        avebox_itr = map_rows2nestable.values().iterator();
        while ( avebox_itr.hasNext() ) {
            rowed_avebox = (TimeAveBox) avebox_itr.next();
            rowed_avebox.setNestingExclusion();
            rowed_avebox.initializeCategoryTimeBoxes();
            if ( isZeroTimeOrigin )
                SummaryState.setTimeBoundingBox( rowed_avebox,
                                                 0.0d,
                                                 super.getDuration() );
            else
                SummaryState.setTimeBoundingBox( rowed_avebox,
                                                 super.getEarliestTime(),
                                                 super.getLatestTime() );
        }

        // For Nestlesses, i.e. arrows
        map_rows2nestless  = new HashMap();
        entry_itr = map_lines2nestless.entrySet().iterator();
        while ( entry_itr.hasNext() ) {
            entry        = (Map.Entry)  entry_itr.next();
            lined_key    = (List)       entry.getKey();
            lined_avebox = (TimeAveBox) entry.getValue();

            rowed_key    = getLine2RowMappedKey( map_line2row, lined_key );
            rowed_avebox = (TimeAveBox) map_rows2nestless.get( rowed_key );
            if ( rowed_avebox == null ) {
                rowed_avebox = new TimeAveBox( lined_avebox );
                map_rows2nestless.put( rowed_key, rowed_avebox );
            }
            else
                rowed_avebox.mergeWithTimeAveBox( lined_avebox );
        }

        // Initialize map_rows2nestless
        avebox_itr = map_rows2nestless.values().iterator();
        while ( avebox_itr.hasNext() ) {
            rowed_avebox = (TimeAveBox) avebox_itr.next();
            rowed_avebox.initializeCategoryTimeBoxes();
            if ( isZeroTimeOrigin )
                SummaryArrow.setTimeBoundingBox( rowed_avebox,
                                                 0.0d,
                                                 super.getDuration() );
            else
                SummaryArrow.setTimeBoundingBox( rowed_avebox,
                                                 super.getEarliestTime(),
                                                 super.getLatestTime() );
        }

        SummaryState.setBackgroundColor( background_color );
        init_state_height_ftr  = init_state_height;
        next_state_height_ftr  = next_state_height;
    }

    public int  drawAllStates( Graphics2D       g,
                               CoordPixelXform  coord_xform )
    {
        Map.Entry          entry;
        Object[]           key;
        Iterator           entries;
        Topology           topo;
        TimeAveBox         avebox;
        float              rStart, rFinal, avebox_hgt;
        int                rowID, count;

        if ( !drawStates )
            return 0;

        count      = 0;
        avebox_hgt = (init_state_height_ftr - next_state_height_ftr) / 2.0f;
        entries    = map_rows2nestable.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry     = (Map.Entry) entries.next();
            key       = ( (List) entry.getKey() ).toArray();
            avebox    = (TimeAveBox) entry.getValue();

            topo      = (Topology) key[0];
            rowID     = ( (Integer) key[1] ).intValue();

            rStart    = (float) rowID - init_state_height_ftr / 2.0f; 
            rFinal    = rStart + init_state_height_ftr;

            count    += SummaryState.draw( g, avebox, coord_xform,
                                           rStart, rFinal, avebox_hgt );
        }
        return count;
    }

    public int  drawAllArrows( Graphics2D       g,
                               CoordPixelXform  coord_xform )
    {
        Map.Entry          entry;
        Object[]           key;
        Iterator           entries;
        Topology           topo;
        TimeAveBox         avebox;
        int                rowID1, rowID2;
        float              rStart, rFinal;
        int                count;

        if ( !drawArrows )
            return 0;

        count   = 0;
        entries = map_rows2nestless.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry     = (Map.Entry) entries.next();
            key       = ( (List) entry.getKey() ).toArray();
            avebox    = (TimeAveBox) entry.getValue();

            topo      = (Topology) key[0];
            rowID1    = ( (Integer) key[1] ).intValue();
            rowID2    = ( (Integer) key[2] ).intValue();

            rStart    = (float) rowID1;
            rFinal    = (float) rowID2;

            count    += SummaryArrow.draw( g, avebox, coord_xform,
                                           rStart, rFinal );
        }
        return count;
    }

    public Summarizable  getSummarizableAt( CoordPixelXform  coord_xform,
                                            Point            pix_pt )
    {
        Map.Entry          entry;
        Object[]           key;
        Iterator           entries;
        Topology           topo;
        TimeAveBox         avebox;
        Object             clicked_obj;
        float              rStart, rFinal, avebox_hgt;
        int                rowID1, rowID2;

        topo         = null;
        rowID1       = -1;
        rowID2       = -1;
        key          = null;
        clicked_obj  = null;

        if ( drawArrows ) {
            entries = map_rows2nestless.entrySet().iterator();
            while ( entries.hasNext() && clicked_obj == null ) {
                entry       = (Map.Entry) entries.next();
                key         = ( (List) entry.getKey() ).toArray();
                avebox      = (TimeAveBox) entry.getValue();

                topo        = (Topology) key[0];
                rowID1      = ( (Integer) key[1] ).intValue();
                rowID2      = ( (Integer) key[2] ).intValue();

                rStart      = (float) rowID1;
                rFinal      = (float) rowID2;

                clicked_obj = SummaryArrow.containsPixel( avebox,
                                                          coord_xform, pix_pt,
                                                          rStart, rFinal );
            }
            if ( clicked_obj != null )
                return new Summarizable( clicked_obj, topo, rowID1, rowID2 );
        }

        if ( drawStates ) {
            avebox_hgt = (init_state_height_ftr - next_state_height_ftr) / 2.0f;
            entries    = map_rows2nestable.entrySet().iterator();
            while ( entries.hasNext() && clicked_obj == null ) {
                entry       = (Map.Entry) entries.next();
                key         = ( (List) entry.getKey() ).toArray();
                avebox      = (TimeAveBox) entry.getValue();

                topo        = (Topology) key[0];
                rowID1      = ( (Integer) key[1] ).intValue();

                rStart      = (float) rowID1 - init_state_height_ftr / 2.0f; 
                rFinal      = rStart + init_state_height_ftr;

                clicked_obj = SummaryState.containsPixel( avebox,
                                                          coord_xform, pix_pt,
                                                          rStart, rFinal,
                                                          avebox_hgt );
            }
            if ( clicked_obj != null )
                return new Summarizable( clicked_obj, topo, rowID1, rowID1 );
        }

        return null;
    }
}

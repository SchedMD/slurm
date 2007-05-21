/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.awt.Graphics2D;
import java.awt.Stroke;
import java.awt.BasicStroke;
import java.awt.Insets;
import java.awt.Color;
import java.awt.Point;
import java.util.Arrays;
import java.util.Set;
import java.util.TreeSet;
import java.util.Map;
import java.util.HashMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Stack;
import java.util.Iterator;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
import base.topology.Line;
import base.topology.PreviewEvent;
import base.topology.PreviewState;
/*
import base.topology.Arrow;
import base.topology.State;
*/

public class Shadow extends Primitive
                    implements MixedDataIO
{
    private static final int     BYTESIZE   = TimeBoundingBox.BYTESIZE /*super*/
                                            + 8  /* num_real_objs */
                                            + 4  /* map_type2twgt's size() */;

    private              long                num_real_objs;

    private              CategoryWeight[]    twgt_ary;           // For Input

    // For SLOG2 Ouput, map_type2twgt ?= null determines getByteSize().
    private              Map                 map_type2twgt;      // For Output
    private              Map                 map_type2dobjs;     // For Output
    private              Set                 set_nestables;      // For Output 
    private              List                list_childshades;   // For Ouput;

    private              Category            selected_subtype;   // For Jumpshot
    private              int                 total_pixel_height; // For Jumpshot

    // For SLOG-2 Input
    public Shadow()
    {
        super();
        num_real_objs       = 0;
        twgt_ary            = null;
        map_type2dobjs      = null;
        map_type2twgt       = null;
        set_nestables       = null;
        list_childshades    = null;
        selected_subtype    = null;
        total_pixel_height  = 0;
    }

    // For SLOG-2 Output : a copy constructor when merging 2 BufForShadows
    public Shadow( final Shadow shade )
    {
        super( shade );
        num_real_objs       = shade.num_real_objs;

        twgt_ary            = null;
        map_type2twgt       = new HashMap();
        // map_type2dobjs, set_nestables & list_childshades are NOT needed
        map_type2dobjs      = null;
        set_nestables       = null;
        list_childshades    = null;

        //  Make a deep copy of shade's map_type2twgt
        CategoryWeight  shade_twgt;
        Iterator        shade_twgts_itr;
        shade_twgts_itr = shade.map_type2twgt.values().iterator();
        while ( shade_twgts_itr.hasNext() ) {
            shade_twgt  = (CategoryWeight) shade_twgts_itr.next();
            map_type2twgt.put( shade_twgt.getCategory(),
                               new CategoryWeight( shade_twgt ) );
        }

        selected_subtype    = null;   // meaningless for SLOG-2 Output
        total_pixel_height  = 0;      // meaningless for SLOG-2 Output
    }

    // For SLOG-2 Output : a real constructor when adding new real Primitive
    public Shadow( Category shadow_type, final Primitive prime )
    {
        super( shadow_type, prime );
        num_real_objs       = 1;

        //  map_type2dobjs and set_nestables are needed in this constructor
        twgt_ary            = null;
        map_type2twgt       = null;
        map_type2dobjs      = new HashMap();

        // Update both map_type2dobjs and set_nestables
        List  dobj_list;
        dobj_list       = new ArrayList();
        dobj_list.add( prime );
        map_type2dobjs.put( prime.getCategory(), dobj_list );

        if ( shadow_type.getTopology().isState() ) {
            set_nestables     = new TreeSet( Drawable.INCRE_STARTTIME_ORDER );
            set_nestables.add( prime );
            list_childshades  = new ArrayList();
        }
        else {
            set_nestables     = null;
            list_childshades  = null;
        }

        selected_subtype    = null;   // meaningless for SLOG-2 Output
        total_pixel_height  = 0;      // meaningless for SLOG-2 Output
    }

    public void mergeWithPrimitive( final Primitive prime )
    {
        Coord[] prime_vtxs = prime.getVertices();
        Coord[] shade_vtxs = super.getVertices();

        if ( prime_vtxs.length != shade_vtxs.length ) {
            String err_msg = "Shadow.mergeWithPrimitive(): ERROR! "
                           + "Incompatible Topology between "
                           + "Shadow and Primitive.";
            throw new IllegalArgumentException( err_msg );
            // System.exit( 1 );
        }

        // do a Time Average over the total number of real drawables
        for ( int idx = 0; idx < shade_vtxs.length; idx++ )
            shade_vtxs[ idx ].time = aveOverAllObjs( shade_vtxs[ idx ].time,
                                                     this.num_real_objs,
                                                     prime_vtxs[ idx ].time,
                                                     1 );
        super.affectTimeBounds( prime );

        // Need to figure out how to do error estimation.
        // Maybe <X^2> is needed to compute the standard dev..
        // time_err = ( super.getLatestTime() - super.getEarliestTime() ) / 2.0;
        num_real_objs++;

        // Update both map_type2dobjs and set_nestables
        List dobj_list = (List) map_type2dobjs.get( prime.getCategory() );
        if ( dobj_list == null ) {
            dobj_list = new ArrayList();
            dobj_list.add( prime );
            map_type2dobjs.put( prime.getCategory(), dobj_list );
        }
        else
            dobj_list.add( prime );

        // if ( super.getCategory().getTopology().isState() )
        if ( set_nestables != null )
            set_nestables.add( prime );
    }

    public void mergeWithShadow( final Shadow sobj )
    {
        // System.err.println( "Shadow.mergeWithShadow(): START" );
        // System.err.println( "\tThe     Shadow=" + this );
        // System.err.println( "\tAnother Shadow=" + sobj );
        Coord[] sobj_vtxs  = sobj.getVertices();
        Coord[] shade_vtxs = super.getVertices();

        if ( sobj_vtxs.length != shade_vtxs.length ) {
            String err_msg = "Shadow.mergeWithShadow(): ERROR! "
                           + "Incompatible Topology between "
                           + "the 2 Shadows.";
            throw new IllegalArgumentException( err_msg );
            // System.exit( 1 );
        }

        double old_duration, new_duration;
        old_duration = super.getDuration();
        // do a Time Average over the total number of real drawables
        for ( int idx = 0; idx < shade_vtxs.length; idx++ )
            shade_vtxs[ idx ].time = aveOverAllObjs( shade_vtxs[ idx ].time,
                                                     this.num_real_objs,
                                                     sobj_vtxs[ idx ].time,
                                                     sobj.num_real_objs );
        super.affectTimeBounds( sobj );
        new_duration = super.getDuration();

        // Need to figure out how to do error estimation.
        // Maybe <X^2> is needed to compute the standard dev..
        // time_err = ( super.getLatestTime() - super.getEarliestTime() ) / 2.0;
        num_real_objs += sobj.num_real_objs;

        // Don't check for (map_type2twgt == null) so it coredumps with trace

        // Since this class's TimeBoundingBox has been affected by sobj,
        // all map_type2twgt must adjust their weight accordingly. 
        CategoryWeight this_twgt, sobj_twgt;
        Iterator       this_twgts_itr, sobj_twgts_itr;
        float          duration_ratio;
        if ( old_duration != new_duration ) {
            duration_ratio = (float) ( old_duration / new_duration );
            this_twgts_itr = this.map_type2twgt.values().iterator();
            while ( this_twgts_itr.hasNext() ) {
                this_twgt = (CategoryWeight) this_twgts_itr.next(); 
                this_twgt.rescaleAllRatios( duration_ratio ); 
            }
        }

        // Merge with sobj's type_wgt[] with adjustment w.r.t this duration
        Category  sobj_type;
        double sobj_duration  = sobj.getDuration();
        duration_ratio = (float) ( sobj_duration / new_duration );
        sobj_twgts_itr = sobj.map_type2twgt.values().iterator();
        while ( sobj_twgts_itr.hasNext() ) {
            sobj_twgt = (CategoryWeight) sobj_twgts_itr.next();
            sobj_type = sobj_twgt.getCategory();
            this_twgt = (CategoryWeight) this.map_type2twgt.get( sobj_type ); 
            if ( this_twgt == null ) {
                this_twgt = new CategoryWeight( sobj_twgt );// sobj_twgt's clone
                this_twgt.rescaleAllRatios( duration_ratio ); 
                map_type2twgt.put( sobj_type, this_twgt );
            }
            else {
                this_twgt.addDrawableCount( sobj_twgt.getDrawableCount() );
                this_twgt.addAllRatios( sobj_twgt, duration_ratio );
            }
        }

        // if ( super.getCategory().getTopology().isState() )
        if ( set_nestables != null )
            list_childshades.add( sobj );

        // System.err.println( "Shadow.mergeWithShadow(): END" );
    }

    // For SLOG-2 Output API
    private void setNestingExclusion()
    {
        Object[]          childshades;
        Stack             nesting_stack;
        Iterator          dobjs_itr;
        Drawable          curr_dobj, stacked_dobj;

        childshades    = list_childshades.toArray();
        nesting_stack  = new Stack();

        //  Assume dobjs_itr returns in Increasing Starttime order
        dobjs_itr      = set_nestables.iterator();
        while ( dobjs_itr.hasNext() ) {
            curr_dobj  = (Drawable) dobjs_itr.next();
            curr_dobj.initExclusion( childshades );
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

        // set_nestables & list_childshades are NOT used anymore
        list_childshades.clear();
        list_childshades  = null;
        set_nestables.clear();
        set_nestables     = null;
    }

    /*
       For SLOG-2 Output API : set InclusiveDurationRatio in CategoryWeight

       This routine sets or initializes the size of map_type2dobjs which
       determines the size of this shadow object.
    */
    public void initializeMapOfCategoryWeights()
    {
        Iterator        type_dobjs_itr, dobjs_itr;
        Map.Entry       type_dobj;
        List            dobj_list;
        Category        type;
        Drawable        dobj;
        CategoryWeight  twgt;
        double          shadow_duration;
        double          incl_fract;
        float           incl_ratio;

        /*
           Check if map_type2twgt == null to tell if this shadow is created
           as real constructor or shallow copy constructor which map_type2twgt
           has been created.  Creation of map_type2twgt tells getByteSize()
           to use map_type2twgt to determine the disk footprint of this shadow.
        */
        if ( map_type2twgt == null )
            map_type2twgt  = new HashMap();

        shadow_duration  = super.getDuration();
        type_dobjs_itr   = map_type2dobjs.entrySet().iterator();
        while ( type_dobjs_itr.hasNext() ) {
            type_dobj  = (Map.Entry) type_dobjs_itr.next();
            type       = (Category) type_dobj.getKey();
            dobj_list  = (List) type_dobj.getValue();

            // Compute the InclusiveRatio of each Category in this Shadow 
            incl_ratio = 0.0f;
            dobjs_itr  = dobj_list.iterator();
            while ( dobjs_itr.hasNext() ) {
                dobj        = (Drawable) dobjs_itr.next();
                incl_fract  = dobj.getDuration() / shadow_duration;
                incl_ratio += (float) incl_fract;
            }
            twgt  = new CategoryWeight( type, incl_ratio, 0.0f,
                                        dobj_list.size() );
            map_type2twgt.put( type, twgt );
            dobj_list  = null;
        }
    }

    /*
       For SLOG-2 Output API : set ExclusionDurationRatio in CategoryWeight

       This routine finalizes the size of map_type2dobjs.  Whatever the size
       of the map_type2dobjs set by the finitializeMapOfCategoryWeights()
       will be left unchanged, i.e. the disk size of the shadow object will be
       the same when finalizeMapOfCategoryWeights() is called.
    */
    public void finalizeMapOfCategoryWeights()
    {
        Iterator        type_dobjs_itr, dobjs_itr;
        Map.Entry       type_dobj;
        List            dobj_list;
        Category        type;
        Drawable        dobj;
        CategoryWeight  twgt;
        double          shadow_duration;
        double          excl_fract;
        float           excl_ratio;

        // Check if this Shadow is of nestable type, i.e. state.
        if ( set_nestables != null ) {
            this.setNestingExclusion();

            //  Finalize/Update the map_type2twgt
            shadow_duration  = super.getDuration();
            type_dobjs_itr   = map_type2dobjs.entrySet().iterator();
            while ( type_dobjs_itr.hasNext() ) {
                type_dobj  = (Map.Entry) type_dobjs_itr.next();
                type       = (Category) type_dobj.getKey();
                dobj_list  = (List) type_dobj.getValue();

                // Compute the ExclusiveRatio of each Category in this Shadow 
                excl_ratio = 0.0f;
                dobjs_itr  = dobj_list.iterator();
                while ( dobjs_itr.hasNext() ) {
                    dobj        = (Drawable) dobjs_itr.next();
                    excl_fract  = dobj.getExclusion() / shadow_duration;
                    excl_ratio += (float) excl_fract;
                }
                twgt  = (CategoryWeight) map_type2twgt.get( type );
                twgt.addExclusiveRatio( excl_ratio );
                dobj_list  = null;
            }
        }

        // map_type2dobjs is NOT used anymore
        if ( map_type2dobjs != null ) {
            map_type2dobjs.clear();
            map_type2dobjs = null;  // set to null so toString() works
        }
    }

    private static double aveOverAllObjs( double sobj_time, long sobj_Nobjs,
                                          double dobj_time, long dobj_Nobjs )
    {
        return ( ( sobj_time * sobj_Nobjs + dobj_time * dobj_Nobjs )
               / ( sobj_Nobjs + dobj_Nobjs ) );
    }

    public int getByteSize()
    {
        if ( twgt_ary != null )  // For SLOG-2 Input
            return super.getByteSize() + BYTESIZE
                 + CategoryWeight.BYTESIZE * twgt_ary.length;
        else if ( map_type2twgt != null )  // For SLOG-2 Output
            return super.getByteSize() + BYTESIZE
                 + CategoryWeight.BYTESIZE * map_type2twgt.size();
        else // if ( map_type2dobjs != null )  // For SLOG-2 Output
            return super.getByteSize() + BYTESIZE
                 + CategoryWeight.BYTESIZE * map_type2dobjs.size();
    }

    // For SLOG-2 Input API, used by BufForShadows.readObject()
    public boolean resolveCategory( Map categorymap )
    {
        boolean  allOK = super.resolveCategory( categorymap );
        if ( twgt_ary != null )
            for ( int idx = twgt_ary.length-1; idx >= 0; idx-- )
                allOK = allOK && twgt_ary[ idx ].resolveCategory( categorymap );
        return allOK;
    }

    // For SLOG-2 Input API i.e. Jumpshot
    public CategoryWeight[] arrayOfCategoryWeights()
    {
        return twgt_ary;
    }

    // For SLOG-2 Input API i.e. Jumpshot
    public Category getSelectedSubCategory()
    {
        return selected_subtype;
    }

    // For SLOG-2 Input API i.e. Jumpshot
    public void clearSelectedSubCategory()
    {
        selected_subtype = null;
    }

    // For SLOG-2 Input API i.e. Jumpshot
    public void setTotalPixelHeight( int new_height )
    {
        total_pixel_height  = new_height;
    }

    // For SLOG-2 Input API i.e. Jumpshot
    public int  getTotalPixelHeight()
    {
        return total_pixel_height;
    }

    public long getNumOfRealObjects()
    {
        return num_real_objs;
    }

    public void summarizeCategories( double buf4sobjs_duration )
    {
        CategoryWeight   this_twgt;
        CategorySummary  type_smy;
        Iterator         this_twgts_itr;
        float            duration_ratio;

        duration_ratio = (float) ( super.getDuration() / buf4sobjs_duration );
        this_twgts_itr = this.map_type2twgt.values().iterator();
        while ( this_twgts_itr.hasNext() ) {
            this_twgt = (CategoryWeight) this_twgts_itr.next(); 
            type_smy  = this_twgt.getCategory().getSummary();
            type_smy.addDrawableCount( this_twgt.getDrawableCount() );
            type_smy.addAllRatios( this_twgt, duration_ratio );
        }
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        super.writeObject( outs );
        TimeBoundingBox.writeObject( this, outs );
        outs.writeLong( num_real_objs );
     // System.err.println( "\touts.size=" + ((DataOutputStream)outs).size() );
        if ( this.map_type2twgt.size() > 0 ) {
            Object[]  twgts;
            twgts = this.map_type2twgt.values().toArray();
            Arrays.sort( twgts, CategoryWeight.INCL_RATIO_ORDER );
            int  twgts_length = twgts.length;
            outs.writeInt( twgts_length );
            for ( int idx = 0; idx < twgts_length; idx++ )
                ((CategoryWeight) twgts[ idx ]).writeObject( outs );
        }
        else
            outs.writeInt( 0 );
    }

    public Shadow( MixedDataInput ins )
    throws java.io.IOException
    {
        super();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        super.readObject( ins );
        TimeBoundingBox.readObject( this, ins );
        num_real_objs = ins.readLong();

        CategoryWeight  twgt;
        int             Nentries, ientry;

        Nentries   = ins.readInt();
        if ( Nentries > 0 ) {
            twgt_ary = new CategoryWeight[ Nentries ];
            for ( ientry = 0; ientry < Nentries; ientry++ )
                twgt_ary[ ientry ] = new CategoryWeight( ins );
        }
        else
            twgt_ary = null;
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( super.toString() );
        rep.append( " Nrobjs=" + num_real_objs );

        CategoryWeight[]  twgts = null;
        if ( twgt_ary != null )
            twgts = twgt_ary;
        else
            if ( map_type2twgt != null )
                twgts = (CategoryWeight[]) map_type2twgt.values().toArray();

        if ( twgts != null ) {
            int  twgts_length = twgts.length;
            for ( int idx = 0; idx < twgts_length; idx++ )
                rep.append( "\n" + twgts[ idx ] );
        }
        return rep.toString();
    }



    private static Insets   Empty_Border   = new Insets( 0, 2, 0, 2 );
    public static void setStateInsetsDimension( int width, int height )
    {
        Empty_Border = new Insets( height, width, height, width );
    }


    // Implementation of abstract methods.

    /* 
        0.0f < nesting_ftr <= 1.0f
    */
    public  int  drawState( Graphics2D g, CoordPixelXform coord_xform,
                            Map map_line2row, DrawnBoxSet drawn_boxes,
                            ColorAlpha color )
    {
        // Coord  start_vtx, final_vtx;
        // start_vtx = this.getStartVertex();
        // final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = super.getEarliestTime();    /* different from Primitive */
        tFinal = super.getLatestTime();      /* different from Primitive */

        int    rowID;
        float  nesting_ftr;
        rowID       = super.getRowID();
        nesting_ftr = super.getNestingFactor();

        // System.out.println( "\t" + this + " nestftr=" + nesting_ftr );

        float  rStart, rFinal;
        rStart = (float) rowID - nesting_ftr / 2.0f;
        rFinal = rStart + nesting_ftr;

        return PreviewState.draw( g, color,
                                  this, Empty_Border, coord_xform,
                                  drawn_boxes.getLastStatePos( rowID ),
                                  tStart, rStart, tFinal, rFinal );
        // return State.draw( g, color, Empty_Border, coord_xform,
        //                    drawn_boxes.getLastStatePos( rowID ),
        //                    tStart, rStart, tFinal, rFinal );
    }



    private static long     Arrow_Log_Base = 10;
    private static Stroke[] Line_Strokes;
    static {
         Line_Strokes = new Stroke[ 10 ];
         for ( int idx = Line_Strokes.length-1; idx >=0 ; idx-- )
             Line_Strokes[ idx ] = new BasicStroke( (float) (idx+1) );
    }

    public static void setBaseOfLogOfObjectNumToArrowWidth( int new_log_base )
    {
        Arrow_Log_Base = (long) new_log_base;
    }

    private static  Stroke  getArrowStroke( long  inum )
    {
        int  idx;
        for ( idx = 0; idx < Line_Strokes.length; idx++ ) {
             inum /= Arrow_Log_Base;
             if ( inum == 0 )
                 break;
        }
        if ( idx < Line_Strokes.length )
            return Line_Strokes[ idx ];
        else
            return Line_Strokes[ Line_Strokes.length-1 ];
    }

    public  int  drawArrow( Graphics2D g, CoordPixelXform coord_xform,
                            Map map_line2row, DrawnBoxSet drawn_boxes,
                            ColorAlpha color )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = super.getEarliestTime();    /* different from Primitive */
        tFinal = super.getLatestTime();      /* different from Primitive */

        int    iStart, iFinal;
        iStart = ( (Integer)
                   map_line2row.get( new Integer(start_vtx.lineID) )
                 ).intValue();
        iFinal = ( (Integer)
                   map_line2row.get( new Integer(final_vtx.lineID) )
                 ).intValue();

        Stroke  arrow_stroke = getArrowStroke( num_real_objs );

        return Line.draw( g, color, arrow_stroke, coord_xform,
                          drawn_boxes.getLastArrowPos( iStart, iFinal ),
                          tStart, (float) iStart, tFinal, (float) iFinal );
    }

    public  int  drawEvent( Graphics2D g, CoordPixelXform coord_xform,
                            Map map_line2row, DrawnBoxSet drawn_boxes,
                            ColorAlpha color )
    {
        Coord  vtx;
        vtx = this.getStartVertex();

        double tStart, tFinal;
        tStart = super.getEarliestTime();    /* different from Primitive */
        tFinal = super.getLatestTime();      /* different from Primitive */

        double tPoint;
        tPoint = vtx.time;

        int    rowID;
        float  rPeak, rStart, rFinal;
        rowID  = ( (Integer)
                   map_line2row.get( new Integer(vtx.lineID) )
                 ).intValue();
        // rPeak  = (float) rowID + NestingStacks.getHalfInitialNestingHeight();
        rPeak  = (float) rowID - 0.25f;
        rStart = (float) rowID - 0.5f;
        rFinal = rStart + 1.0f;

        return PreviewEvent.draw( g, color, null, coord_xform,
                                 drawn_boxes.getLastEventPos( rowID ),
                                 tStart, rStart, tFinal, rFinal,
                                 tPoint, rPeak );
    }

    /* 
        0.0f < nesting_ftr <= 1.0f
    */
    public  boolean isPixelInState( CoordPixelXform coord_xform,
                                    Map map_line2row, Point pix_pt )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = super.getEarliestTime();    /* different from Primitive */
        tFinal = super.getLatestTime();      /* different from Primitive */

        int    rowID;
        float  nesting_ftr;
        /*
        rowID  = ( (Integer)
                   map_line2row.get( new Integer(start_vtx.lineID) )
                 ).intValue();
        */
        rowID       = super.getRowID();
        /* assume NestingFactor has been calculated */
        nesting_ftr = super.getNestingFactor();

        // System.out.println( "\t" + this + " nestftr=" + nesting_ftr );

        float  rStart, rFinal;
        rStart = (float) rowID - nesting_ftr / 2.0f;
        rFinal = rStart + nesting_ftr;

        selected_subtype = PreviewState.containsPixel( this, Empty_Border,
                                                       coord_xform, pix_pt,
                                                       tStart, rStart,
                                                       tFinal, rFinal );
        return selected_subtype != null;
    }

    //  assume this Shadow overlaps with coord_xform.TimeBoundingBox
    public  boolean isPixelOnArrow( CoordPixelXform coord_xform,
                                    Map map_line2row, Point pix_pt )
    {
        Coord  start_vtx, final_vtx;
        start_vtx = this.getStartVertex();
        final_vtx = this.getFinalVertex();

        double tStart, tFinal;
        tStart = super.getEarliestTime();    /* different from Primitive */
        tFinal = super.getLatestTime();      /* different from Primitive */

        float  rStart, rFinal;
        rStart = ( (Integer)
                   map_line2row.get( new Integer(start_vtx.lineID) )
                 ).floatValue();
        rFinal = ( (Integer)
                   map_line2row.get( new Integer(final_vtx.lineID) )
                 ).floatValue();

        return Line.containsPixel( coord_xform, pix_pt,
                                   tStart, rStart, tFinal, rFinal );
    }

    public  boolean isPixelAtEvent( CoordPixelXform coord_xform,
                                    Map map_line2row, Point pix_pt )
    {
        Coord  vtx;
        vtx = this.getStartVertex();

        double tStart, tFinal;
        tStart = super.getEarliestTime();    /* different from Primitive */
        tFinal = super.getLatestTime();      /* different from Primitive */

        double tPoint;
        tPoint = vtx.time;

        int    rowID;
        float  rPeak, rStart, rFinal;
        rowID  = ( (Integer)
                   map_line2row.get( new Integer(vtx.lineID) )
                 ).intValue();
        // rPeak  = (float) rowID + NestingStacks.getHalfInitialNestingHeight();
        rPeak  = (float) rowID - 0.25f;
        rStart = (float) rowID - 0.5f;
        rFinal = rStart + 1.0f;

        return PreviewEvent.containsPixel( coord_xform, pix_pt,
                                           tStart, rStart, tFinal, rFinal,
                                           tPoint, rPeak );
    }

    public boolean containSearchable()
    {
        CategoryWeight  twgt;
        int             idx;

        for ( idx = twgt_ary.length-1; idx >= 0; idx-- ) {
             twgt = twgt_ary[ idx ];
             if ( twgt.getCategory().isVisiblySearchable() )
                 return true;
        }
        return false;
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.input;

import java.util.Iterator;

import base.drawable.TimeBoundingBox;
import base.drawable.Drawable;
import logformat.slog2.*;

/*
    The TreeTrunk consists all of the TreeNodes that are specified by
    a TimeBoundingBox and a lowest_depth_reached( read ).
*/
public class TreeTrunk extends TreeFloorList
{
    public  final static int              TIMEBOX_DISJOINTED = -1;
    public  final static int              TIMEBOX_EQUAL      = 0;
    public  final static int              TIMEBOX_SCROLLING  = 1;
    public  final static int              TIMEBOX_ZOOMING    = 2;

    private       static boolean          isDebugging = false;
    private       static TimeBoundingBox  timeframe_root;
    private              short            depth_root;  // depth of treeroot
    private              short            depth_init;  // depth initialized
    private              short            iZoom_level;

    private              double           duration_root;
    private              double           tZoomFactor;
    private              double           logZoomFactor;
    
    private              InputLog         slog_ins;
    private              Drawable.Order   node_dobj_order;

    public TreeTrunk( InputLog  in_slog, final Drawable.Order in_dobj_order )
    {
        super( in_dobj_order );
        slog_ins       = in_slog;
        timeframe_root = null;

        /*
           Assume treenode is read in Drawable.INCRE_STARTTIME_ORDER
           so if the input Drawable.Order's isStartTimeOrdered() is true,
           then no reordering is needed.  If isStartTimeOrdered() returns
           false, the treenode's BufForObjects{Drawables,Shadows} will then
           be reshuffled in the order of Drawable.INCRE_FINALTIME_ORDER.
        */
        node_dobj_order = in_dobj_order.isStartTimeOrdered() ?
                          null :
                          Drawable.INCRE_FINALTIME_ORDER;
            
    }

    public void initFromTreeTop()
    {
        FileBlockPtr  blockptr;
        TreeNode      treeroot;

        blockptr       = slog_ins.getFileBlockPtrToTreeRoot();
        if ( blockptr.isNULL() )
            return;
        treeroot       = slog_ins.readTreeNode( blockptr );
        if ( treeroot == null )
            return;

        if ( node_dobj_order != null )
            treeroot.reorderDrawables( node_dobj_order );
        depth_root     = treeroot.getTreeNodeID().depth;
        timeframe_root = new TimeBoundingBox( treeroot );
        duration_root  = timeframe_root.getDuration();
        depth_init     = depth_root;
        iZoom_level    = 0;
        tZoomFactor    = (double) slog_ins.getNumChildrenPerNode();
        logZoomFactor  = Math.log( tZoomFactor );
        super.init( depth_root );
        super.put( treeroot, treeroot );
    }

    /*
        setTimeZoomFactor() set Non-Default zooming factor.  
        It is Not being used yet
    */
    public void setTimeZoomFactor( double time_zoom_factor )
    {
        tZoomFactor    = time_zoom_factor;
        logZoomFactor  = Math.log( tZoomFactor );
    }

    public double getTimeZoomFactor()
    {
        return tZoomFactor;
    }

    public TreeNode getTreeRoot()
    {
        return (TreeNode) super.getRoot();
    }

    public void growInTreeWindow( TreeNode treenode, short in_depth_init,
                                  final TimeBoundingBox  time_win )
    {
        depth_init = in_depth_init;
        this.growChildren( treenode, depth_init, time_win );
        super.updateLowestDepth();
    }

    /*
      growChildren() is NOT to be called by anyone
      except growInTreeWindow() or {zoom,scroll}TimeWindowTo()
      Assumption:  treenode.overlaps( time_win ) == true;
    */
    private void growChildren( final TreeNode treenode, short in_depth,
                               final TimeBoundingBox  time_win )
    {
        BufForObjects[]  childstubs;
        BufForObjects    childstub;
        TreeNode         childnode;
        FileBlockPtr     blockptr;
        int              idx;

        if (    treenode != null
             && treenode.getTreeNodeID().depth > in_depth ) {
            if ( treenode.overlaps( time_win ) ) {
                childstubs = treenode.getChildStubs();
                for ( idx = 0; idx < childstubs.length; idx++ ) {
                    childstub = childstubs[ idx ];
                    childnode = super.get( childstub );
                    if ( childstub.overlaps( time_win ) ) {
                        if ( childnode == null ) {
                            blockptr   = childstub.getFileBlockPtr();
                            childnode  = slog_ins.readTreeNode( blockptr );
                            if ( node_dobj_order != null )
                                childnode.reorderDrawables( node_dobj_order );
                            super.put( childstub, childnode );
                        }
                        // Invoke growChildren() again to make sure all 
                        // childnode's decendants are in memory even if
                        // childnode is already in memory, because childnode's
                        // in-memory descendants may NOT overlap time_win.
                        this.growChildren( childnode, in_depth, time_win );
                    }
                    else { // childstub.disjoints( time_win )
                        if ( childnode != null ) {
                            this.removeChildren( childnode, in_depth );
                            if ( isDebugging )
                                debug_println( "TreeTrunk.growChildren(): "
                                + "remove(" + childstub.getTreeNodeID() + ")" );
                            super.remove( childstub );
                        }
                    }
                }
            }
            else {  // if ( treenode.disjoints( time_win ) )
                String err_msg = "TreeTrunk.growChildren(): ERROR!\n"
                               + "\t treenode.overlaps( time_win ) "
                               + "!= true\n"
                               + "\t " + treenode.getTreeNodeID()
                               + "does NOT overlap with " + time_win; 
                throw new IllegalStateException( err_msg );
                // System.exit( 1 );
            }
        }
    }

    /*
       removeChildren() does NOT remove input argument, treenode, so
       super.remove( childstub ) is needed after removeChildren( childnode )
    */
    private void removeChildren( final TreeNode treenode, short in_depth )
    {
        BufForObjects[]  childstubs;
        BufForObjects    childstub;
        TreeNode         childnode;
        int              idx;

        if (    treenode != null
             && treenode.getTreeNodeID().depth > in_depth ) {
            childstubs = treenode.getChildStubs();
            for ( idx = 0; idx < childstubs.length; idx++ ) {
                childstub = childstubs[ idx ];
                childnode = super.get( childstub );
                if ( childnode != null ) {
                    this.removeChildren( childnode, in_depth );
                    if ( isDebugging )
                        debug_println( "TreeTrunk.removeChildren(): "
                        + "remove(" + childstub.getTreeNodeID() + ")" );
                    super.remove( childstub );
                }
            }
        }
    }

    /*
        Adjust the duration of the root level treefloor's time window.
        So that zoom level can be computed consistently, i.e. getZoomLevel().
        CanvasTime needs to call this.
    */
    public void setNumOfViewsPerUpdate( int num_views )
    {
        duration_root *= (double) num_views;
    }

    /* return 0 when time_win.getDuration() == timeframe_root.getDuration() */
    private short getZoomLevel( final TimeBoundingBox  time_win )
    {
        return (short) Math.round( Math.log( duration_root
                                           / time_win.getDuration() )
                                 / logZoomFactor );
    }

    // Zoom In/Out :
    // The argument, time_win, is the new Time Window to be achieved
    public void zoomTimeWindowTo( final TimeBoundingBox  time_win )
    {
        if ( isDebugging )
            debug_println( "zoomTimeWindowTo( " + time_win + " )" );
        TreeFloor  coverer, lowester;
        short      coverer_depth, lowester_depth, next_depth;
        TreeNode   treenode;

        iZoom_level     = this.getZoomLevel( time_win );
        lowester        = super.getLowestFloor();
        // if ( ! lowester.covers( time_win ) ) {
            coverer         = super.getCoveringFloor( time_win );
            coverer_depth   = coverer.getDepth();
            lowester_depth  = lowester.getDepth();
            // guarantee zoom-in to be reverse function of zoom-out; 
            next_depth      = (short) ( depth_init - iZoom_level );
            if ( next_depth < 0 )
                next_depth = 0;
            if ( next_depth > depth_root )
                next_depth = depth_root;
            if ( isDebugging ) {
                debug_println( "coverer_depth = " + coverer_depth );
                debug_println( "lowester_depth = " + lowester_depth );
                debug_println( "iZoom_level = " + iZoom_level );
                debug_println( "next_depth = " + next_depth );
            }
            if ( next_depth < coverer_depth ) {
                Iterator nodes = coverer.values().iterator();
                while ( nodes.hasNext() ) {
                    treenode = (TreeNode) nodes.next();
                    if ( treenode.overlaps( time_win ) )
                        this.growChildren( treenode, next_depth, time_win );
                    else {
                        this.removeChildren( treenode, next_depth );
                        if ( isDebugging )
                            debug_println( "TreeTrunk.zoomTimeWindowTo(): "
                            + "remove(" + treenode.getTreeNodeID() + ")" );
                        nodes.remove();
                    }
                }
                super.removeAllChildFloorsBelow( next_depth );
            }
        // }
        // return super.getLowestDepth();
        if ( isDebugging )
            debug_println( super.toStubString() );
    }


    // Scroll forward and backward
    // The argument, time_win, is the new Time Window to be achieved
    // Returns the lowest-depth of the tree.
    public void scrollTimeWindowTo( final TimeBoundingBox  time_win )
    {
        if ( isDebugging )
            debug_println( "scrollTimeWindowTo( " + time_win + " )" );
        TreeFloor  coverer, lowester;
        short      coverer_depth, lowester_depth, next_depth;
        TreeNode   treenode;

        lowester        = super.getLowestFloor();
        if ( ! lowester.covers( time_win ) ) {
            coverer         = super.getCoveringFloor( time_win );
            coverer_depth   = coverer.getDepth();
            lowester_depth  = lowester.getDepth();
            next_depth      = lowester_depth;
            if ( isDebugging ) {
                debug_println( "coverer_depth = " + coverer_depth );
                debug_println( "lowester_depth = " + lowester_depth );
                debug_println( "iZoom_level = " + iZoom_level );
                debug_println( "next_depth = " + next_depth );
            }
            if ( next_depth < coverer_depth ) {
                Iterator nodes = coverer.values().iterator();
                while ( nodes.hasNext() ) {
                    treenode = (TreeNode) nodes.next();
                    if ( treenode.overlaps( time_win ) )
                        this.growChildren( treenode, next_depth, time_win );
                    else {
                        this.removeChildren( treenode, next_depth );
                        if ( isDebugging )
                            debug_println( "TreeTrunk.scrollTimeWindowTo(): "
                            + "remove(" + treenode.getTreeNodeID() + ")" );
                        nodes.remove();
                    }
                }
                // super.updateLowestDepth();
                super.removeAllChildFloorsBelow( next_depth );
            }
        }
        // return super.getLowestDepth();
        if ( isDebugging )
            debug_println( super.toStubString() );
    }

    // Float.MIN_VALUE is way to small, set TOLERANCE to 1%
    // private static final double TOLERANCE = 5 * Float.MIN_VALUE;
    private static final double TOLERANCE = 0.01f;

    //  This function returns
    //       TIMEBOX_DISJOINTED  if time_win_new is disjoint from tree_rootj.
    //       TIMEBOX_EQUAL       if time_win_new == time_win_old
    //       TIMEBOX_SCROLLING   if it is scrolling.
    //       TIMEBOX_ZOOMING     if it is zooming.
    public int updateTimeWindow( final TimeBoundingBox  time_win_old,
                                 final TimeBoundingBox  time_win_new )
    {
        // Error Checking
        /*
        if ( ! super.getLowestFloor().covers( time_win_old ) ) {
            debug_println( "TreeTrunk.updateTimeWindow(): WARNING!\n"
                         + "\t LowestFloorTimeWin = "
                         + super.getLowestFloor().getTimeBounds()
                         + " does NOT cover TimeWin_old.\n"
                         + "\t TimeWin_old = " + time_win_old + ".\n"
                         + "\t TimeWin_new = " + time_win_new + ".\n" );
        }
        */

        double time_ratio;
        if ( timeframe_root.overlaps( time_win_new ) ) { 
            //  Determine if TimeWindow is scrolled, enlarged or contracted.
            if ( ! time_win_old.equals( time_win_new ) ) {
                time_ratio = time_win_new.getDuration()
                           / time_win_old.getDuration();
                if ( Math.abs( time_ratio - 1.0d ) <= TOLERANCE ) {
                    scrollTimeWindowTo( time_win_new );
                    return TIMEBOX_SCROLLING;
                }
                else {
                    zoomTimeWindowTo( time_win_new );
                    return TIMEBOX_ZOOMING;
                }
            }   
            return TIMEBOX_EQUAL;
        }
        else {  // if ( timeframe_root.disjoints( time_win_new ) )
            //  Don't update the TimeWindow, emit a warning message and return
            if ( isDebugging )
            debug_println( "TreeTrunk.updateTimeWindow(): ERROR!\n"
                         + "\t TimeWindow disjoints from TimeFrame@TreeRoot.\n"
                         + "\t TimeWin@TreeRoot = " + timeframe_root + "\n"
                         + "\t TimeWin_old      = " + time_win_old   + "\n"
                         + "\t TimeWin_new      = " + time_win_new   + "\n" );
            return TIMEBOX_DISJOINTED;
        }
    }

    public void setDebuggingEnabled( boolean bvalue )
    {
        isDebugging = bvalue;
    }

    public boolean isDebugging()
    {
        return isDebugging;
    }

    private static void debug_println( String str )
    {
        System.out.println( str );
    }
}

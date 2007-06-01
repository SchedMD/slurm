/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.util.Stack;
import javax.swing.JTree;

public class NestingStacks
{
    //  0.0 < Nesting_Height_Reduction < 1.0
    private static  float  Nesting_Height_Reduction      = 0.8f;
    private static  float  Initial_Nesting_Height        = 0.8f;
    private static  float  Half_Initial_Nesting_Height   = 0.4f;
    private static  float  Shadow_Nesting_Height         = 0.4f;

    private JTree          tree_view;
    private Stack[]        nesting_stacks;
    private boolean        hasNestingStacksBeenUsed;
    private boolean        isTimeBoxScrolling;

    private int            num_rows;

    public NestingStacks( final JTree  in_tree_view )
    {
        tree_view       = in_tree_view;
        nesting_stacks  = null;
    }

    public static void  setNestingHeightReduction( float new_reduction )
    {
        if ( new_reduction > 0.0f && new_reduction < 1.0f )
            Nesting_Height_Reduction = new_reduction;
    }

    public static void  setInitialNestingHeight( float new_init_height )
    {
        if ( new_init_height > 0.0f && new_init_height < 1.0f ) {
            Initial_Nesting_Height      = new_init_height;   
            Half_Initial_Nesting_Height = Initial_Nesting_Height / 2.0f;
            Shadow_Nesting_Height       = Initial_Nesting_Height;
        }
    }

    public static float getHalfInitialNestingHeight()
    {
        return Half_Initial_Nesting_Height;
    }

    public void initialize( boolean isScrolling )
    {
        //  Need to check to see if tree_view has been updated,
        //  If not, no need to construct all these Stack[].
        isTimeBoxScrolling = isScrolling;
        num_rows           = tree_view.getRowCount();
        nesting_stacks     = new Stack[ num_rows ];
        for ( int irow = 0 ; irow < num_rows ; irow++ ) {
            //  Select only non-expanded row
            if ( ! tree_view.isExpanded( irow ) )
                nesting_stacks[ irow ] = new Stack();
            else
                nesting_stacks[ irow ] = null;
        }
        hasNestingStacksBeenUsed = false;
    }

    public void reset()
    {
        if ( hasNestingStacksBeenUsed ) {
            for ( int irow = 0 ; irow < num_rows ; irow++ ) {
                //  Select only non-expanded row
                if ( nesting_stacks[ irow ] != null )
                    nesting_stacks[ irow ].clear();
            }
        }
        else
            hasNestingStacksBeenUsed = true;
    }

    // Cannot use finalize() as function name,
    // finalize() overrides Object.finalize().
    public void finish()
    {
        for ( int irow = 0 ; irow < num_rows ; irow++ ) {
            //  Select only non-expanded row
            if ( nesting_stacks[ irow ] != null )
                nesting_stacks[ irow ] = null;
        }
    }

    public boolean isReadyToGetNestingFactorFor( Drawable cur_dobj )
    {
        if ( this.isTimeBoxScrolling )
            return cur_dobj.isNestingFactorUninitialized();
        else
            return true;
    }

    /*
       Given the NestingStack of the timeline that the Drawable is on,
       Compute the NestingFactor, nesting_ftr.

       It seems NestingFactor cannot be cached, as Zoom-In operations
       bring in the real Drawables that are represented by the Shadows.
       These Drawables needs all the other real Drawables to be presented
       on the NestingStack to have their NestingFactor computed correctly.

       Assume all drawables are arranged in increasing starttime order

       In order to guarantee that both real drawables and shadows which
       may overlap on another but not totally enclosed one another, a
       scheme is needed to make sure they can be drawn and displayed
       continuously/seamlessly across different images in ScrollableObject.
       Object in the nesting stack will be popped until a totally
       covering drawable is found, i.e dobj.covers( cur_dobj ).
       The previous criterion,  
           cur_dobj.getEarliestTime() < dobj.getLatestTime()
       is not enough to guarantee a continuous and seamless drawables
       across images.  Overlapped drawables cannot be drawn nested, because
       when overlapped drawables spread between 2 neighoring images, they 
       may not be both within in 2 neigboring images.  The nesting_stack 
       algorithm seems to work faultlessly for perfectly nested states 
       across different images.  Therefore, extending criteria of popping
       nesting stack to search of perfect nested states seem doing the trick. 
     */
    public float getNestingFactorFor( final Drawable cur_dobj )
    {
        // boolean   isRealDrawable  = ! ( cur_dobj instanceof Shadow );
        int       rowID           = cur_dobj.getRowID();
        Stack     nesting_stack   = nesting_stacks[ rowID ];
        float     nesting_ftr     = Drawable.NON_NESTABLE;
        Drawable  dobj;

        while ( ! nesting_stack.empty() ) {
            dobj = (Drawable) nesting_stack.peek();
            // cur_dobj is nested inside. pop Overlaped for image continuity
            if ( dobj.covers( cur_dobj ) ) {
                nesting_ftr = dobj.getNestingFactor()
                            * Nesting_Height_Reduction;
                nesting_stack.push( cur_dobj );
                return nesting_ftr;
            }
            else
                nesting_stack.pop();
        }
        // i.e.  nesting_stack.empty() == true
        nesting_ftr = Initial_Nesting_Height;
        nesting_stack.push( cur_dobj );
        return nesting_ftr;
    }   //  Endof public float getNestingFactorFor()
}

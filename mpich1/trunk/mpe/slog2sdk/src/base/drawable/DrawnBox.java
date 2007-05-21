/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

public class DrawnBox
{
    private static final int   TOLERANCE = 1;

    /*
       Both head and tail are used by Arrow and State.
       But only head is used by Event.
    */
    private              int   head;
    private              int   tail;

    public DrawnBox()
    {
        head = Integer.MIN_VALUE;
        tail = Integer.MIN_VALUE;
    }

    public void reset()
    {
        head = Integer.MIN_VALUE;
        tail = Integer.MIN_VALUE;
    }

    public boolean coversState( int new_head, int new_tail )
    {
        int new_width = new_tail - new_head;
        if ( new_width <= TOLERANCE )
            return    Math.abs( new_tail - tail ) <= TOLERANCE
                   || Math.abs( new_head - head ) <= TOLERANCE;
        /*
        if ( new_width == 0 )
            return  new_tail == tail || new_head == head;
        */
        return false;
    }

    public boolean coversArrow( int new_head, int new_tail )
    {
        return    Math.abs( new_tail - tail ) <= TOLERANCE
               && Math.abs( new_head - head ) <= TOLERANCE;
    }

    public boolean coversEvent( int new_point )
    {
        return Math.abs( new_point - head ) <= TOLERANCE;
    }

    public void set( int new_head, int new_tail )
    {
        head = new_head;
        tail = new_tail;
    }

    public void set( int new_point )
    {
        head = new_point;
    }

    public int getHead()
    {
        return head;
    }

    public int getTail()
    {
        return tail;
    }
}

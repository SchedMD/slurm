/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.border.*;

import viewer.common.Const;

public class ScrollbarTime extends JScrollBar
{
    private ModelTime   model;
    private Dimension   min_size;

    public ScrollbarTime( ModelTime model )
    {
        super( JScrollBar.HORIZONTAL );
        this.model = model;

        setModel( model );
        this.addAdjustmentListener( model );

        super.setUnitIncrement( Const.TIME_SCROLL_UNIT_INIT );

        min_size = super.getMinimumSize();
        if ( min_size.height <= 0 )
            min_size.height = 20;
    }

    public void setBlockIncrementToModelExtent()
    {
        if ( model != null ) {
            int model_extent = model.getExtent();
            if ( model_extent > 1 )
                super.setBlockIncrement( model_extent );
        }
    }

    public Dimension getMinimumSize()
    {
        return min_size;
    }

    public void init()
    {
        // int id = AdjustmentEvent.ADJUSTMENT_VALUE_CHANGED;
        // int type = AdjustmentEvent.TRACK;
        // super.fireAdjustmentValueChanged( id, type, super.getValue() );
    }
}

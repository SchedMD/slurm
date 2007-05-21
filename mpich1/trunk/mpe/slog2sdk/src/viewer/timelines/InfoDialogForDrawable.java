/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.awt.*;
import javax.swing.*;
import java.util.Map;

import base.drawable.Drawable;
import viewer.zoomable.InfoDialog;

public class InfoDialogForDrawable extends InfoDialog
{
    public InfoDialogForDrawable( final Frame     frame, 
                                  final double    clicked_time,
                                  final Map       map_line2treenodes,
                                  final String[]  y_colnames,
                                  final Drawable  clicked_dobj )
    {
        super( frame, "Drawable Info Box", clicked_time );

        Container root_panel = this.getContentPane();
        root_panel.setLayout( new BoxLayout( root_panel, BoxLayout.Y_AXIS ) );

        root_panel.add( new InfoPanelForDrawable( map_line2treenodes,
                                                  y_colnames, clicked_dobj ) );

        root_panel.add( super.getCloseButtonPanel() );
    }
}

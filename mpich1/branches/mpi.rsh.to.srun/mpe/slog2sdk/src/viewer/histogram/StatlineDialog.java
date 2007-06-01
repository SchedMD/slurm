/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.histogram;

import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

import base.drawable.TimeBoundingBox;
import base.statistics.BufForTimeAveBoxes;
import logformat.slog2.LineIDMap;
import viewer.zoomable.InitializableDialog;

public class StatlineDialog extends InitializableDialog
{
    private StatlinePanel  top_panel;

    public StatlineDialog( final Dialog              ancestor_dialog,
                           final TimeBoundingBox     timebox,
                           final LineIDMap           lineIDmap,
                           final BufForTimeAveBoxes  buf4statboxes )
    {
        super( ancestor_dialog, "Histogram for the duration [ "
                              + (float)timebox.getEarliestTime() + ", "
                              + (float)timebox.getLatestTime() + " ]" );
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );
 
        top_panel = new StatlinePanel( this, timebox,
                                       lineIDmap, buf4statboxes );
        setContentPane( top_panel );

        addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent e ) {
                StatlineDialog.this.dispose();
            }
        } );
    }

    public void init()
    {
        top_panel.init();
    }
}

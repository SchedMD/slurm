/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import javax.swing.*;
import javax.swing.event.*;

import logformat.slog2.input.TreeTrunk;
import viewer.common.Const;
import viewer.common.LabeledTextField;

public class TreeTrunkPanel extends JPanel
                            implements ChangeListener
{
    private TreeTrunk               treetrunk;
    private LabeledTextField        fld_low2max_depth;
    private short                   max_depth;

    public TreeTrunkPanel( TreeTrunk  in_treetrunk )
    {
        super();
        treetrunk  = in_treetrunk;
        setLayout( new BoxLayout( this, BoxLayout.X_AXIS ) );

        fld_low2max_depth   = new LabeledTextField( "Lowest / Max. Depth",
                                                    null );
        fld_low2max_depth.setEditable( false );
        fld_low2max_depth.setHorizontalAlignment( JTextField.CENTER );
        // fld_tree_depth.addActionListener( this );
        add( fld_low2max_depth );

        super.setBorder( BorderFactory.createEtchedBorder() );
        max_depth = treetrunk.getTreeRoot().getTreeNodeID().depth;
    }

    // public void lowestDepthChanged()
    public void stateChanged( ChangeEvent evt )
    {
        StringBuffer  strbuf = new StringBuffer();
        strbuf.append( treetrunk.getLowestDepth() );
        strbuf.append( " / " );
        strbuf.append( max_depth );
        fld_low2max_depth.setText( strbuf.toString() );
    }
    
}

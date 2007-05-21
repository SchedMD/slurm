/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.Set;
import java.util.Map;
import java.util.TreeMap;
import java.util.Iterator;
import java.io.DataInput;
import java.io.DataOutput;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.DataIO;
import base.io.MixedDataIO;

// TreeNodeID will be stored in TreeNodeID.INCRE_INDEX_ORDER.
// .i.e. equivalent to Drawable.INCRE_STARTTIME_ORDER.
public class TreeDir extends TreeMap
                     implements DataIO, MixedDataIO
{
    private TreeNodeID  root;

    public TreeDir()
    {
        super( TreeNodeID.INCRE_INDEX_ORDER );
        root = new TreeNodeID( (short) 0, 0 );
    }

    public TreeNodeID getTreeRootID()
    {
        return root;
    }

    // Overload put()/get() of Map interface to do what we want.
    public void put( TreeNodeID ID, TreeDirValue entry_val )
    {
        super.put( ID, entry_val );
        if ( ID.isPossibleRoot() && ID.depth > root.depth )
            root = new TreeNodeID( ID );
    }

    public TreeDirValue get( TreeNodeID ID )
    {
        return (TreeDirValue) super.get( ID );
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        TreeNodeID    ID;
        TreeDirValue  val;
        Map.Entry     entry;

        outs.writeInt( super.size() );
        Iterator entries = this.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry   = (Map.Entry) entries.next();
            ID      = (TreeNodeID) entry.getKey();
            val     = (TreeDirValue) entry.getValue();
            ID.writeObject( outs );
            val.writeObject( outs );
        }
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        this.writeObject( (DataOutput) outs );
    }

    public TreeDir( DataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        TreeNodeID    ID;
        TreeDirValue  val;
        int           Nentries;

        Nentries = ins.readInt();
        for ( int idx = 0; idx < Nentries; idx++ ) {
            ID      = new TreeNodeID( ins );
            val     = new TreeDirValue( ins );
            this.put( ID, val );
        }
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        this.readObject( (DataInput) ins );
    }

    public String toString()
    {
        TreeNodeID    ID;
        TreeDirValue  val;
        Map.Entry     entry;

        StringBuffer rep = new StringBuffer( "\t SLOG-2 Tree Directory\n" );
        Iterator entries = this.entrySet().iterator();
        while ( entries.hasNext() ) {
            entry   = (Map.Entry) entries.next();
            ID      = (TreeNodeID) entry.getKey();
            val     = (TreeDirValue) entry.getValue();
            rep.append( ID + " -> " + val + "\n" );
        }
        return rep.toString();
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
import base.io.MixedRandomAccessFile;

public class Header implements MixedDataIO
{
    public static final int
        BYTESIZE = MixedRandomAccessFile.getStringByteSize( Const.version_ID )
                 + 2  /* num_children_per_node */
                 + 4  /* treeleaf_bytesize */
                 + 2  /* max_treedepth */
                 + 4  /* max_buffer_bytesize */
                 + 7 * FileBlockPtr.BYTESIZE ;

    private String          version_ID;
    private short           num_children_per_node;
    private int             treeleaf_bytesize;
    private short           max_treedepth;
    private int             max_buffer_bytesize;

    // leave FileBlockPtr type public to allow easy access
    public  FileBlockPtr    blockptr2categories;
    public  FileBlockPtr    blockptr2methoddefs;
    public  FileBlockPtr    blockptr2lineIDmaps;
    public  FileBlockPtr    blockptr2treeroot;
    public  FileBlockPtr    blockptr2treedir;
    public  FileBlockPtr    blockptr2annotations;
    public  FileBlockPtr    blockptr2postamble;

    public Header()
    {
        // version_ID assignment of Const.version_ID is for SLOG-2 Output API.
        // But it should NOT affect the Input API, because version_ID is
        // updated by readObject() later.
        version_ID              = Const.version_ID;
        num_children_per_node   = Const.NUM_LEAFS;
        treeleaf_bytesize       = Const.LEAF_BYTESIZE;
        max_treedepth           = 0;
        max_buffer_bytesize     = 0;

        // Initialize all the FileBlockPtrs so FileBlockPtr.isNULL() == true
        blockptr2categories     = new FileBlockPtr();
        blockptr2methoddefs     = new FileBlockPtr();
        blockptr2lineIDmaps     = new FileBlockPtr();
        blockptr2treeroot       = new FileBlockPtr();
        blockptr2treedir        = new FileBlockPtr();
        blockptr2annotations    = new FileBlockPtr();
        blockptr2postamble      = new FileBlockPtr();

	// System.out.println( "SLOG version = " + version_ID );
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        outs.writeString( version_ID );
        outs.writeShort( num_children_per_node );
        outs.writeInt( treeleaf_bytesize );
        outs.writeShort( max_treedepth );
        outs.writeInt( max_buffer_bytesize );
        blockptr2categories.writeObject( outs );
        blockptr2methoddefs.writeObject( outs );
        blockptr2lineIDmaps.writeObject( outs );
        blockptr2treeroot.writeObject( outs );
        blockptr2treedir.writeObject( outs );
        blockptr2annotations.writeObject( outs );
        blockptr2postamble.writeObject( outs );
    }

    public Header( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        short  max_str_length;
        max_str_length          = (short) Const.version_ID.length();
        version_ID              = ins.readStringWithLimit( max_str_length );
        num_children_per_node   = ins.readShort();
        treeleaf_bytesize       = ins.readInt();
        max_treedepth           = ins.readShort();
        max_buffer_bytesize     = ins.readInt();
        blockptr2categories.readObject( ins );
        blockptr2methoddefs.readObject( ins );
        blockptr2lineIDmaps.readObject( ins );
        blockptr2treeroot.readObject( ins );
        blockptr2treedir.readObject( ins );
        blockptr2annotations.readObject( ins );
        blockptr2postamble.readObject( ins );
    }

    // For SLOG-2 Input API
    public boolean isSLOG2()
    {
        return version_ID != null && version_ID.startsWith( Const.SLOG2_ID );
    }

    // For SLOG-2 Input API
    public String getCompatibleVersionMessage()
    {
        if (    version_ID != null
             && version_ID.compareTo( Const.version_ID ) == 0 ) 
            return null;
        else
            return ( "Incompatible Version IDs detected! \n"
                   + "The logfile's version ID is "
                   + version_ID + ",\n"
                   + "but this input program reads logfile of version "
                   + Const.version_ID + ".\n" );
    }

    public void setTreeLeafByteSize( int in_bytesize )
    {
        treeleaf_bytesize   = in_bytesize;
    }

    public int getTreeLeafByteSize()
    {
       	return treeleaf_bytesize;
    }

    public void setNumChildrenPerNode( short num_leafs )
    {
        num_children_per_node = num_leafs;
    }

    public short getNumChildrenPerNode()
    {
        return num_children_per_node;
    }

    public void setMaxTreeDepth( short in_depth )
    {
        max_treedepth = in_depth;
    }

    public short getMaxTreeDepth()
    {
        return max_treedepth;
    }

    public void setMaxBufferByteSize( int bufsize )
    {
        max_buffer_bytesize = bufsize;
    }

    public int  getMaxBufferByteSize()
    {
        return max_buffer_bytesize;
    }

    public String toString()
    {
        StringBuffer rep = new StringBuffer( "\t SLOG-2 Header:\n" );
        rep.append( "version = " + version_ID + "\n" );
        rep.append( "NumOfChildrenPerNode = " + num_children_per_node + "\n" );
        rep.append( "TreeLeafByteSize = " + treeleaf_bytesize + "\n" );
        rep.append( "MaxTreeDepth = " + max_treedepth + "\n" );
        rep.append( "MaxBufferByteSize = " + max_buffer_bytesize + "\n" );
        rep.append( "Categories  is " + blockptr2categories  + "\n" );
        rep.append( "MethodDefs  is " + blockptr2methoddefs  + "\n" );
        rep.append( "LineIDMaps  is " + blockptr2lineIDmaps  + "\n" );
        rep.append( "TreeRoot    is " + blockptr2treeroot    + "\n" );
        rep.append( "TreeDir     is " + blockptr2treedir     + "\n" );
        rep.append( "Annotations is " + blockptr2annotations + "\n" );
        rep.append( "Postamble   is " + blockptr2postamble   + "\n" );
        return rep.toString();
    }
}

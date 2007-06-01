/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.output;

import java.io.*;

import base.io.MixedRandomAccessFile;
import base.io.BufArrayOutputStream;
import base.io.MixedDataOutputStream;
import base.io.DataIO;
import base.io.MixedDataIO;
import base.drawable.TimeBoundingBox;
import logformat.slog2.*;

public class OutputLog
{
    private MixedRandomAccessFile  rand_file;
    private BufArrayOutputStream   bary_outs;
    private MixedDataOutputStream  data_outs;

    private Header                 filehdr;
    private TreeDir                treedir;

    // Last Treenode's FileBlockPtr
    private FileBlockPtr           node_blockptr;

    public OutputLog( String filename )
    {
        rand_file = null;
        try {
            rand_file = new MixedRandomAccessFile( filename, "rw" );
        // } catch ( FileNotFoundException ferr ) {
        } catch ( IOException ferr ) {
            ferr.printStackTrace();
            System.exit( 1 );
        }
        
        filehdr        = new Header();

        /*
           Const.LEAF_BYTESIZE is the INITIAL size for
           BufArrayOutputStream's buffer which can still grow as needed
        */
        bary_outs      = new BufArrayOutputStream(
                                 filehdr.getTreeLeafByteSize() );
        data_outs      = new MixedDataOutputStream( bary_outs );

        treedir        = new TreeDir();
        node_blockptr  = null;

        // Write the initialized Header to Disk
        this.writeHeader();
    }

    public void setTreeLeafByteSize( int in_bytesize )
    {
        filehdr.setTreeLeafByteSize( in_bytesize );
        bary_outs      = new BufArrayOutputStream( 
                                 filehdr.getTreeLeafByteSize() );
        data_outs      = new MixedDataOutputStream( bary_outs );
    }

    public int getTreeLeafByteSize()
    {
        return filehdr.getTreeLeafByteSize();
    }

    public void setNumChildrenPerNode( short num_leafs )
    {
        filehdr.setNumChildrenPerNode( num_leafs );
    }

    public short getNumChildrenPerNode()
    {
        return filehdr.getNumChildrenPerNode();
    }

    public void writeTreeNode( TreeNode treenode )
    {
        TreeNodeID             treedir_key;
        TreeDirValue           treedir_val;
        TimeBoundingBox        timebox;

        try {
            // Save the LAST input TreeNode's FileBlockPtr
            node_blockptr = new FileBlockPtr( rand_file.getFilePointer(),
                                              treenode.getNodeByteSize() );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }

        // Create key and value pair for the TreeDir
        treedir_key   = new TreeNodeID( treenode.getTreeNodeID() );
        timebox = new TimeBoundingBox( treenode );
        treedir_val   = new TreeDirValue( timebox, node_blockptr );
        treedir.put( treedir_key, treedir_val );

        // Reset various file pointers of the TreeNode before writing to file
        treenode.setFileBlockPtr( node_blockptr.getFilePointer(),
                                  node_blockptr.getBlockSize() );

        // treenode's mergeVerticalShadowBufs() and shiftHorizontalShadowBuf()
        // updates ShadowBufs which are NOT being saved to disk in this call.

        // Merge ShadowBufs Vertically from childnode's
        treenode.mergeVerticalShadowBufs();
        // The TreeNode.shiftHorizontalShadowBuf() has to be called
        // after TreeNode.setFileBlockPtr( node_blockptr )
        treenode.shiftHorizontalShadowBuf();

        // System.err.println( "OutputLog.writeTreeNode("
        //                   + treenode.getTreeNodeID() + "): START" );

        // Clear any unwritten data in BufArrayOutputStream's buffer
        bary_outs.reset();
        try {
            // Save TreeNode to BufArrayOutputStream through DataOutputStream
            treenode.writeObject( data_outs );
            data_outs.flush();
            // Copy the content of BufArrayOutputStream to RandomAccessFile
            rand_file.write( bary_outs.getByteArrayBuf(),
                             0, bary_outs.size() );
            if ( bary_outs.size() > filehdr.getMaxBufferByteSize() )
                filehdr.setMaxBufferByteSize( bary_outs.size() );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }

        // System.err.println( "OutputLog.writeTreeNode("
        //                   + treenode.getTreeNodeID() + "): END" );

        /* debugging */
        // System.out.println( "\n" + treenode.toString() + "\n" );
        // System.out.flush();
    }

    private void writeFilePart( final MixedDataIO   filepart,
                                      FileBlockPtr  blockptr )
    {
        int          blocksize  = 0;

        try {
            // Update FilePointer of the Header's FileBlockPtr to "filepart" 
            blockptr.setFilePointer( rand_file.getFilePointer() );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }

        // Clear any unwritten data in BufArrayOutputStream's buffer
        bary_outs.reset();
        try {
            // filepart.writeObject() needs a MixedDataOutput type argument
            filepart.writeObject( data_outs );
            data_outs.flush();
            // Copy the content of BufArrayOutputStream to RandomAccessFile
            rand_file.write( bary_outs.getByteArrayBuf(),
                             0, bary_outs.size() );
            if ( bary_outs.size() > filehdr.getMaxBufferByteSize() )
                filehdr.setMaxBufferByteSize( bary_outs.size() );
            blocksize = (int)( rand_file.getFilePointer()
                             - blockptr.getFilePointer() );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }

        // Update BlockSize of the Header's FileBlockPtr to "filepart"
        blockptr.setBlockSize( blocksize );

        /* debugging */
        // System.out.println( filepart.toString() );
        // System.out.flush();
    }

    public void writeCategoryMap( final CategoryMap objdefs )
    {
        writeFilePart( objdefs, filehdr.blockptr2categories );
    }

    public void writeLineIDMapList( final LineIDMapList lineIDmaps )
    {
        writeFilePart( lineIDmaps, filehdr.blockptr2lineIDmaps );
    }

    private void writeTreeDir()
    {
        writeFilePart( treedir, filehdr.blockptr2treedir );
    }

    private void writeHeader()
    {
        // Update the Header if necessary
        if ( node_blockptr != null ) {
            filehdr.blockptr2treeroot.setFileBlockPtr(
                node_blockptr.getFilePointer(), node_blockptr.getBlockSize() );
            filehdr.setMaxTreeDepth( treedir.getTreeRootID().depth );
        }

        // Clear any unwritten data in BufArrayOutputStream's buffer
        bary_outs.reset();
        try {
            filehdr.writeObject( data_outs );
            data_outs.flush();
            // Copy the content of BufArrayOutputStream to RandomAccessFile
            rand_file.seek( 0 );
            rand_file.write( bary_outs.getByteArrayBuf(),
                             0, bary_outs.size() );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }
    }

    public void close()
    {
        this.writeTreeDir();
        // System.out.println( treedir.toString() );
        this.writeHeader();
        System.out.println( filehdr.toString() );

        try {
            // ? data_outs.close() calls bary_outs.close();
            data_outs.close();
            rand_file.close();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            System.exit( 1 );
        }
    }

    protected void finalize() throws Throwable
    {
        try {
            close();
        } finally {
            super.finalize();
        }
    }
}

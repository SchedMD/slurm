/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2.update;

import java.util.Map;
import java.util.HashMap;
import java.awt.Color;

import old_logformat.slog2.pipe.PipedInputLog;

import base.io.MixedDataIO;
import base.drawable.InfoType;
import base.drawable.Method;
import base.drawable.ColorAlpha;
import base.drawable.Kind;
import base.drawable.Topology;
import base.drawable.Category;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Composite;
import base.drawable.YCoordMap;
import base.drawable.InputAPI;
import logformat.slog2.LineIDMap;

/*
   Since old_logformat.slog2.pipe.PipedInputLog has already extended from 
   old_base.drawable.InputAPI which is similar to base.drawable.InputAPI.
   If this class extends old_logformat.slog2.pipe.PipedInputLog and implements
   base.drawable.InputAPI, this class essentially extends both
   base.drawable.InputAPI and old_base.drawable.InputAPI which are conflicting
   to each other, e.g. getNextKind() is returning both old_base.drawable.Kind
   as well as base.drawable.Kind.  Since old_base.drawable.InputAPI is
   being implemented first in the class hierarchy, old_base.drawable.InputAPI
   will be the one found when compiling this class.  The causes compilation
   failure.
   
   public class WrappedInputLog extends PipedInputLog
                                implements InputAPI
*/

public class WrappedInputLog implements InputAPI
{
    private int                   MEM_PIPE_RESIZE_MAX  = 10;
    private int                   KILOBYTES            = 1024;

    private PipedInputLog         old_dobj_ins;

    // The following map could be made static ?
    private Map                   kind_map;
    private Map                   topo_map;

    private MemoryPipedStream     mem_pipe;
    private int                   mem_pipe_resize_count;

    /*
       updateObject()'s mem_pipe receives old_obj as input and
       spits new_obj as output.  Since the mechanism relies on MixedDataIO
       interface, the output new_obj will be corrected as long as
       the beginning portion of the memory layout done by the writeObject()
       matches that done by the readObject().
    */
    private void updateObject( final old_base.io.MixedDataIO  old_obj,
                                     MixedDataIO              new_obj )
    {
        mem_pipe.reset();
        try {
            old_obj.writeObject( mem_pipe.output );
            new_obj.readObject( mem_pipe.input );
        } catch ( java.io.EOFException eoferr ) {
            if ( mem_pipe_resize_count < MEM_PIPE_RESIZE_MAX ) {
                mem_pipe.resizeBuffer( mem_pipe.getBufferSize() * 2 );
                mem_pipe_resize_count++;
                updateObject( old_obj, new_obj );
            }
            else {
                System.err.println( "WrappedInputLog.updateObject(): "
                                  + "Resizing of memory pipe has exceeded "
                                  + MEM_PIPE_RESIZE_MAX + " times. "
                                  + "The memory pipe's buffer size is "
                                  + mem_pipe.getBufferSize() + " bytes." );
                eoferr.printStackTrace();
                System.exit( 1 );
            }
        } catch ( java.io.IOException ioerr ) {
            System.err.println( "WrappedInputLog.updateObject(): "
                              + "Composite conversion fails! " );
            ioerr.printStackTrace();
            System.exit( 1 );
        }
    }

    public WrappedInputLog( String pathname )
    {
        old_dobj_ins  = new PipedInputLog( pathname );
        /*
           Using Map to convert old to new is slow,
           but avoid exposing the internal of the classes.
        */
        kind_map      = new HashMap();
        kind_map.put( old_base.drawable.Kind.TOPOLOGY,  Kind.TOPOLOGY );
        kind_map.put( old_base.drawable.Kind.EOF,       Kind.EOF );
        kind_map.put( old_base.drawable.Kind.PRIMITIVE, Kind.PRIMITIVE );
        kind_map.put( old_base.drawable.Kind.COMPOSITE, Kind.COMPOSITE );
        kind_map.put( old_base.drawable.Kind.CATEGORY,  Kind.CATEGORY );
        kind_map.put( old_base.drawable.Kind.YCOORDMAP, Kind.YCOORDMAP );

        topo_map      = new HashMap();
        topo_map.put( old_base.drawable.Topology.EVENT, Topology.EVENT );
        topo_map.put( old_base.drawable.Topology.STATE, Topology.STATE );
        topo_map.put( old_base.drawable.Topology.ARROW, Topology.ARROW );
    }

    public boolean isSLOG2()
    { return old_dobj_ins.isSLOG2(); }

    public String getCompatibleHeader()
    { return old_dobj_ins.getCompatibleHeader(); }

    public static void stdoutConfirmation()
    { PipedInputLog.stdoutConfirmation(); }

    public int getTreeLeafByteSize()
    { return old_dobj_ins.getTreeLeafByteSize(); }

    public short getNumChildrenPerNode()
    { return old_dobj_ins.getNumChildrenPerNode(); }

    public void initialize()
    {
        int  buf_size;

        old_dobj_ins.initialize();
        // Make sure the byte[] won't be too small.
        buf_size = Math.max( old_dobj_ins.getTreeLeafByteSize(), KILOBYTES );
        mem_pipe = new MemoryPipedStream( buf_size );
        mem_pipe_resize_count = 0;
    }

    public Kind peekNextKind()
    {
        return (Kind) kind_map.get( old_dobj_ins.peekNextKind() );
    }

    public Topology getNextTopology()
    {
        return (Topology) topo_map.get( old_dobj_ins.getNextTopology() );
    }

    public Category getNextCategory()
    {
        old_base.drawable.Category    old_type;
        Category                      new_type;

        old_type  = old_dobj_ins.getNextCategory();
        new_type  = new Category();
        updateObject( old_type, new_type );

        return new_type;
    }

    public YCoordMap getNextYCoordMap()
    {
        old_base.drawable.YCoordMap   old_ymap;
        YCoordMap                     new_ymap;
        old_logformat.slog2.LineIDMap old_lidmap;
        LineIDMap                     new_lidmap;

        old_ymap   = old_dobj_ins.getNextYCoordMap();
        old_lidmap = new old_logformat.slog2.LineIDMap( old_ymap );
        new_lidmap = new LineIDMap();
        updateObject( old_lidmap, new_lidmap );
        new_ymap   = new_lidmap.toYCoordMap();

        return new_ymap;
    }

    public Primitive getNextPrimitive()
    {
        old_base.drawable.Primitive  old_prime;
        Primitive                    new_prime;

        old_prime  = old_dobj_ins.getNextPrimitive();
        new_prime  = new Primitive();
        updateObject( old_prime, new_prime );

        return new_prime;
    }

    public Composite getNextComposite()
    {
        old_base.drawable.Composite  old_cmplx;
        Composite                    new_cmplx;

        old_cmplx  = old_dobj_ins.getNextComposite();
        new_cmplx  = new Composite();
        updateObject( old_cmplx, new_cmplx );

        return new_cmplx;
    }

    public void close()
    {
        old_dobj_ins.close();
        try {
            mem_pipe.close();
        } catch ( java.io.IOException ioerr ) {
            System.err.println( "WrappedInputLog.close(): "
                              + "Memory pipe fails to close() " );
            ioerr.printStackTrace();
            System.exit( 1 );
        }
    }
}

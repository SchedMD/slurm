/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2TOdrawable;

import logformat.clog2.RecDefEvent;
import logformat.clog2.RecDefState;
import base.drawable.Category;
import base.drawable.Topology;
import base.drawable.ColorAlpha;

public class ObjDef extends Category
{
    private static int  nextCategoryIndex = 0;

    // Limited access to this package
              Integer  start_evt;
              Integer  final_evt;


    public ObjDef( int in_idx, final RecDefState in_def,
                   final Topology in_topo, int in_width )
    {
        super( in_idx, in_def.name, in_topo, 
               ColorNameMap.getColorAlpha( in_def.color ), in_width );
        super.setInfoKeys( in_def.format );
        this.start_evt   = in_def.startetype;
        this.final_evt   = in_def.finaletype;
    }

    public ObjDef( int in_idx, final RecDefEvent in_def,
                   final Topology in_topo, int in_width )
    {
        super( in_idx, in_def.name, in_topo,
               ColorNameMap.getColorAlpha( in_def.color ), in_width );
        super.setInfoKeys( in_def.format );
        this.start_evt   = in_def.etype;
        this.final_evt   = in_def.etype;
    }

    public ObjDef( int in_idx,
                   final RecDefState in_def, final Topology in_topo,
                   final ColorAlpha in_color, int in_width )
    {
        super( in_idx, in_def.name, in_topo, in_color, in_width );
        super.setInfoKeys( in_def.format );
        this.start_evt   = in_def.startetype;
        this.final_evt   = in_def.finaletype;
    }

    public static void setFirstNextCategoryIndex( int ival )
    {
        nextCategoryIndex = ival;
    }

    public static int getNextCategoryIndex()
    {
        return nextCategoryIndex++;
    }

    public String toString()
    {
        return ( "ObjDef{ evts=(" + start_evt + "," + final_evt + "), "
               + super.toString() + " }" );
    }
}

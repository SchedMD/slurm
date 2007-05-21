/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.util.StringTokenizer;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;
// import java.awt.Shape;

public class Category implements MixedDataIO
{
    private int             index;
    private String          name;
    private Topology        topo;         // private Shape    shape;
    private ColorAlpha      color;
    private int             width;
    private String[]        infokeys;     // string infokeys for the infovals
    private InfoType[]      infotypes;    // % token to represent infoval type
    private Method[]        methods;
    private CategorySummary summary;

    private boolean         hasBeenUsed;  // For SLOG-2 Output, remove unused.

    private boolean         isVisible;    // For SLOG-2 Input, or Jumpshot
    private boolean         isSearchable; // For SLOG-2 Input, or Jumpshot


    public Category()
    {
        infokeys     = null;
        infotypes    = null;
        methods      = null;
        summary      = new CategorySummary();
        hasBeenUsed  = false;
        isVisible    = true;
        isSearchable = true;
    }

    public Category( int in_idx, String in_name, Topology in_topo,
                     ColorAlpha in_color, int in_width )
    {
        index        = in_idx;
        name         = in_name;
        topo         = in_topo;
        color        = in_color;
        width        = in_width;
        infokeys     = null;
        infotypes    = null;
        methods      = null;
        summary      = new CategorySummary();
        hasBeenUsed  = false;
        isVisible    = true;
        isSearchable = true;
    }

    //  For TRACE-API where Category is created through DobjDef class.
    public Category( int obj_idx, String obj_name, int obj_width )
    {
        index        = obj_idx;
        name         = obj_name;
        width        = obj_width;
        infokeys     = null;
        infotypes    = null;
        methods      = null;
        summary      = new CategorySummary();
        hasBeenUsed  = false;
        isVisible    = true;
        isSearchable = true;
    }

    public int getIndex()
    {
        return index;
    }

    public void setUsed( boolean new_value )
    {
        hasBeenUsed  = new_value;
    }

    public boolean isUsed()
    {
        return hasBeenUsed;
    }

    public void setVisible( boolean new_value )
    {
        isVisible = new_value;
    }

    public boolean isVisible()
    {
        return isVisible;
    }

    public void setSearchable( boolean new_value )
    {
        isSearchable = new_value;
    }

    public boolean isSearchable()
    {
        return isSearchable;
    }

    public boolean isVisiblySearchable()
    {
        return isVisible && isSearchable;
    }

    public void setName( String in_name )
    {
        name       = in_name;
    }

    public String getName()
    {
        return name;
    }

    public void setTopology( Topology in_topo )
    {
        topo       = in_topo;
    }

    public Topology getTopology()
    {
        return topo;
    }

    public void setColor( ColorAlpha in_color )
    {
        color      = in_color;
    }

    public ColorAlpha getColor()
    {
        return color;
    }

    // For logformat.slog2.update.UpdatedInputLog
    public void setWidth( int in_width )
    {
        width      = in_width;
    }

    // For logformat.slog2.update.UpdatedInputLog
    public int getWidth()
    {
        return width;
    }

    public void setInfoKeys( String labels )
    {
        if ( labels != null ) {
            List             keylist, typelist; 
            StringTokenizer  str_tokens;
            String           str1, str2;
            str_tokens = new StringTokenizer( labels, "%", true );
            int Ntokens = str_tokens.countTokens();
            if ( Ntokens > 0 ) {
                keylist   = new ArrayList();
                typelist  = new ArrayList();
                while ( str_tokens.hasMoreTokens() ) {
                    str1  = str_tokens.nextToken();
                    if ( str1.length() == 1 && str1.equals( "%" ) ) {
                        str2     = str_tokens.nextToken();
                        /* assume only 1 char behind "%" is printing %-token */
                        typelist.add( new InfoType( str2.charAt( 0 ) ) );
                        str1     = str2.substring( 1 ); /* allow empty string */
                    }
                    keylist.add( str1 );  
                }     

                if ( keylist.size() > 0 ) {
                    this.infokeys = new String[ keylist.size() ];
                    Iterator keys = keylist.iterator();
                    for ( int idx = 0; keys.hasNext(); idx++ )
                        this.infokeys[ idx ] = (String) keys.next();
                }
                else
                    this.infokeys = null;

                if ( typelist.size() > 0 ) {
                    this.infotypes = new InfoType[ typelist.size() ];
                    Iterator types = typelist.iterator();
                    for ( int idx = 0; types.hasNext(); idx++ )
                        this.infotypes[ idx ] = (InfoType) types.next();
                }
                else
                    this.infotypes = null;
            }
        }
        else {
            this.infokeys   = null;
            this.infotypes  = null;
        }
    }

    // For logformat.slog2.update.UpdatedInputLog
    public void setInfoKeys( String[] in_infokeys )
    {
        this.infokeys = in_infokeys;
    }

    public String[] getInfoKeys()
    {
        return this.infokeys;
    }

    // For logformat.slog2.update.UpdatedInputLog
    public void setInfoTypes( InfoType[] in_infotypes )
    {
        this.infotypes = in_infotypes;
    }

    public InfoType[] getInfoTypes()
    {
        return this.infotypes;
    }

    // For TRACE-API's DobjDef constructor
    public void setMethodIDs( int[] methodIDs )
    {
        if ( methodIDs != null && methodIDs.length > 0 ) {
            methods = new Method[ methodIDs.length ];
            for ( int idx = 0; idx < methodIDs.length; idx++ )
                methods[ idx ] = new Method( methodIDs[ idx ] );
        }
        else
            methods = null;
    }

    // For logformat.slog2.update.UpdatedInputLog
    public void setMethods( Method[] in_methods )
    {
        methods = in_methods;
    }

    // For logformat.slog2.update.UpdatedInputLog
    public Method[] getMethods()
    {
        return methods;
    } 

    public CategorySummary getSummary()
    {
        return this.summary;
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        int idx;

        outs.writeInt( index );
        outs.writeString( name );
        topo.writeObject( outs );
        color.writeObject( outs );
        outs.writeByte( width );

        if ( infokeys != null && infokeys.length > 0 ) {
            outs.writeShort( (short) infokeys.length );
            for ( idx = 0; idx < infokeys.length; idx++ )
                outs.writeString( infokeys[ idx ] );
        }
        else
            outs.writeShort( 0 );

        if ( infotypes != null && infotypes.length > 0 ) {
            outs.writeShort( (short) infotypes.length );
            for ( idx = 0; idx < infotypes.length; idx++ )
                infotypes[ idx ].writeObject( outs );
        }
        else
            outs.writeShort( 0 );

        if ( methods != null && methods.length > 0 ) {
            outs.writeShort( (short) methods.length );
            for ( idx = 0; idx < methods.length; idx++ )
                methods[ idx ].writeObject( outs );
        }
        else
            outs.writeShort( 0 );

        summary.writeObject( outs );
    }

    public Category( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        int  Nelem, idx;

        index = ins.readInt();
        name  = ins.readString();
        topo  = new Topology( ins );
        color = new ColorAlpha( ins );
        width = (int) ins.readByte();

        Nelem = (int) ins.readShort();
        if ( Nelem > 0 ) {
            infokeys = new String[ Nelem ];
            for ( idx = 0; idx < Nelem; idx++ )
                infokeys[ idx ]   = ins.readString();
        }
        else
            infokeys   = null;

        Nelem = (int) ins.readShort();
        if ( Nelem > 0 ) {
            infotypes = new InfoType[ Nelem ];
            for ( idx = 0; idx < Nelem; idx++ )
                infotypes[ idx ]   = new InfoType( ins );
        }
        else
            infotypes   = null;

        Nelem = (int) ins.readShort();
        if ( Nelem > 0 ) {
            methods = new Method[ Nelem ];
            for ( idx = 0; idx < Nelem; idx++ )
                methods[ idx ]   = new Method( ins );
        }
        else
            methods   = null;

        summary.readObject( ins );
    }

    public String toString()
    {
        int  max_length, infokeys_length, infotypes_length;
        StringBuffer rep = new StringBuffer( "Category[ " );
        rep.append( "index=" + index
                  + ", name=" + name
                  + ", topo=" + topo
                  + ", color=" + color
                  + ", isUsed=" + hasBeenUsed
                  + ", width=" + width );
        if (    ( infokeys != null && infokeys.length > 0 )
             || ( infotypes != null && infotypes.length > 0 ) ) {
            rep.append( ", info_fmt=< " );
            if ( infokeys != null )
                infokeys_length = infokeys.length;
            else
                infokeys_length = 0;
            if ( infotypes != null )
                infotypes_length = infotypes.length;
            else
                infotypes_length = 0;
            max_length = Math.max( infokeys_length, infotypes_length );

            for ( int idx = 0; idx < max_length; idx++ ) {
                if ( idx < infokeys_length )
                    rep.append( infokeys[ idx ] );
                if ( idx < infotypes_length )
                    rep.append( infotypes[ idx ] );
            }
            rep.append( " >" );
        }
        if ( methods != null && methods.length > 0 ) {
            rep.append( ", methods=< " );
            for ( int idx = 0; idx < methods.length; idx++ )
                rep.append( methods[ idx ] + " " );
            rep.append( ">" );
        }
        rep.append( ", vis=" + isVisible );
        rep.append( ", search=" + isSearchable );
        rep.append( ", " + summary );
        rep.append( " ]" );
        return rep.toString();
    }

    private static final int     PREVIEW_EVENT_INDEX = -1;
    private static final int     PREVIEW_ARROW_INDEX = -2;
    private static final int     PREVIEW_STATE_INDEX = -3;

    public boolean isShadowCategory()
    {
        return index < 0;
    }

    // A static function call, because the returned shadow category
    // should be the same for for all TRACE formats.
    public static Category getShadowCategory( final Topology aTopo )
    {
        Category   type;
        if ( aTopo.isEvent() ) {
            type = new Category( PREVIEW_EVENT_INDEX,
                                 "Preview_" + aTopo.toString(),
                                 aTopo, ColorAlpha.WHITE_NEAR_OPAQUE, 5 );
            // type.setInfoKeys( "num_real_objs=%d\ntime_error=%E\n" );
            return type;
        }
        else if ( aTopo.isArrow() ) {
            type = new Category( PREVIEW_ARROW_INDEX,
                                 "Preview_" + aTopo.toString(),
                                 aTopo, ColorAlpha.YELLOW_OPAQUE, 5 );
            // type.setInfoKeys( "num_real_objs=%d\ntime_error=%E\n"
            //                 + "msg_size=%d\n" );
            return type;
        }
        else if ( aTopo.isState() ) {
            type = new Category( PREVIEW_STATE_INDEX,
                                 "Preview_" + aTopo.toString(),
                                 aTopo, ColorAlpha.WHITE_NEAR_OPAQUE, 5 );
            // type.setInfoKeys( "num_real_objs=%d\ntime_error=%E\n" );
            return type;
        }
        return null;
    }
}

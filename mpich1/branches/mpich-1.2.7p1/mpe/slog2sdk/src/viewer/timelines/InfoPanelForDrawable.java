/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.timelines;

import java.text.NumberFormat;
import java.text.DecimalFormat;
import java.awt.*;
import javax.swing.*;
import javax.swing.border.*;
import javax.swing.tree.TreeNode;
import java.util.Map;
import java.util.Iterator;

import base.drawable.Coord;
import base.drawable.Topology;
import base.drawable.Drawable;
import base.drawable.Primitive;
import base.drawable.Composite;
import base.drawable.Shadow;
import base.drawable.Category;
import base.drawable.CategoryWeight;
import base.topology.PreviewState;
import viewer.common.Const;
import viewer.common.Routines;
import viewer.common.Parameters;
import viewer.legends.CategoryLabel;
import viewer.zoomable.TimeFormat;
import viewer.zoomable.SearchPanel;
import viewer.zoomable.YaxisTreeNode;


public class InfoPanelForDrawable extends SearchPanel // SearchPanel is JPanel
{
    private static final Component      STRUT = Box.createHorizontalStrut( 10 );
    private static final Component      GLUE  = Box.createHorizontalGlue();

    private static final String         FORMAT = Const.INFOBOX_TIME_FORMAT;
    private static       DecimalFormat  fmt    = null;
    private static       TimeFormat     tfmt   = null;

    private static       Border         Normal_Border = null;
    private static       Border         Shadow_Border = null;

    private              Drawable       drawable;


    public InfoPanelForDrawable( final Map       map_line2treenodes,
                                 final String[]  y_colnames,
                                 final Drawable  dobj )
    {
        super();
        super.setLayout( new BoxLayout( this, BoxLayout.Y_AXIS ) );

        /* Define DecialFormat for the displayed time */
        if ( fmt == null ) {
            fmt = (DecimalFormat) NumberFormat.getInstance();
            fmt.applyPattern( FORMAT );
        }
        if ( tfmt == null )
            tfmt = new TimeFormat();
        if ( Normal_Border == null ) {
            /*
            Normal_Border = BorderFactory.createCompoundBorder(
                            BorderFactory.createRaisedBevelBorder(),
                            BorderFactory.createLoweredBevelBorder() );
            */
            Normal_Border = BorderFactory.createEtchedBorder();
        }
        if ( Shadow_Border == null ) {
            Shadow_Border = BorderFactory.createTitledBorder(
                                          Normal_Border, " Preview State ",
                                          TitledBorder.LEFT, TitledBorder.TOP,
                                          Const.FONT, Color.magenta );
        }

        drawable   = dobj;

        // Set the CategoryLabel Icon
        Dimension     panel_max_size;
        Category      type       = null;
        CategoryLabel label_type = null;
        JPanel        top_panel  = new JPanel();
        top_panel.setLayout( new BoxLayout( top_panel, BoxLayout.X_AXIS ) );
        if (    drawable instanceof Shadow
             && ( (Shadow) drawable ).getSelectedSubCategory() != null ) {
            type       = ( (Shadow) drawable ).getSelectedSubCategory();
            label_type = new CategoryLabel( type );
            ( (Shadow) drawable ).clearSelectedSubCategory();
            top_panel.setBorder( Shadow_Border );
        }
        else {
            type       = drawable.getCategory();
            label_type = new CategoryLabel( type );
            top_panel.setBorder( Normal_Border );
        }
        top_panel.add( STRUT );
        top_panel.add( label_type );
        top_panel.add( GLUE );
        top_panel.setAlignmentX( Component.LEFT_ALIGNMENT );
        panel_max_size        = top_panel.getPreferredSize();
        panel_max_size.width  = Short.MAX_VALUE;
        top_panel.setMaximumSize( panel_max_size );
        super.add( top_panel );

        // Determine the text of the drawable
        TextAreaBuffer  textbuf;
        int             num_cols, num_rows;
        textbuf = new TextAreaBuffer( map_line2treenodes, y_colnames );
        if ( drawable instanceof Shadow )
            textbuf.setShadowText( (Shadow) drawable, type );
        else if ( drawable instanceof Composite ) 
            textbuf.setCompositeText( (Composite) drawable );
        else
            textbuf.setPrimitiveText( (Primitive) drawable );
        textbuf.finalized();
        num_cols  = textbuf.getColumnCount();
        num_rows  = textbuf.getRowCount();

        // Set the TextArea
        JTextArea  text_area;
        int        adj_num_cols;
        text_area    = new JTextArea( textbuf.toString() );
        adj_num_cols = Routines.getAdjNumOfTextColumns( text_area, num_cols );
        num_cols     = (int) Math.ceil( adj_num_cols * 85.0d / 100.0d );
        text_area.setColumns( num_cols );
        text_area.setRows( num_rows );
        text_area.setEditable( false );
        text_area.setLineWrap( true );
        JScrollPane scroller = new JScrollPane( text_area );
        scroller.setAlignmentX( Component.LEFT_ALIGNMENT );
        super.add( scroller );
    }

    //  The following function is for Seach and Scan facility of the viewport.
    public Drawable  getSearchedDrawable()
    { return drawable; }



    private class TextAreaBuffer
    {
        private              Map            map_line2treenodes;
        private              String[]       y_colnames;
        private              StringBuffer   strbuf;
        private              String         strbuf2str;
        private              int            num_cols;
        private              int            num_rows;

        public TextAreaBuffer( final Map       in_map_line2treenodes,
                               final String[]  in_y_colnames )
        {
            map_line2treenodes  = in_map_line2treenodes;
            y_colnames          = in_y_colnames;
            strbuf              = new StringBuffer();
            strbuf2str          = null;

            // Initialize num_cols and num_rows.
            num_cols            = 0;
            num_rows            = 0;
        }

        // this.finalized() needs to be called before
        // getColumnCount()/getRowCount()/toString()
        public void finalized()
        {
            int num_lines;
            strbuf2str = strbuf.toString();
            num_lines  = this.getNumOfLines();
            if ( num_lines <= 3 )
                num_rows = 3;
            else
                num_rows = 4;
        }

        public int getColumnCount()
        { return num_cols; }

        public int getRowCount()
        { return num_rows; }

        public String toString()
        { return strbuf2str; }

        private int getNumOfLines()
        {
            int num_lines;
            int str_length;
            int ipos;
            if ( strbuf2str != null ) {
                num_lines  = 1;
                ipos       = 0;
                str_length = strbuf2str.length();
                while ( ipos >= 0 && ipos < str_length ) {
                    ipos = strbuf2str.indexOf( '\n', ipos );
                    if ( ipos >= 0 ) {
                        num_lines++;
                        ipos++;
                    }
                }
                return num_lines;
            }
            else
                return -1;
        }

        private void setCoordsText( final Coord[]   coords,
                                          String    description )
        {
            StringBuffer   linebuf;
            Coord          vertex;
            YaxisTreeNode  node;
            TreeNode[]     nodes;
            Integer        lineID;
            double         duration;
            int            coords_length;
            int            idx, ii;
    
            linebuf = new StringBuffer();
            coords_length = coords.length;
            if ( coords_length > 1 ) {
                duration = coords[ coords_length-1 ].time - coords[ 0 ].time;
                linebuf.append( "duration" + description
                              + " = " + tfmt.format( duration ) );
                if ( num_cols < linebuf.length() )
                    num_cols = linebuf.length();
                num_rows++;
                strbuf.append( linebuf.toString() + "\n" );
            }
            for ( idx = 0; idx < coords_length; idx++ ) {
                linebuf = new StringBuffer( "[" + idx + "]: " );
                vertex  = coords[ idx ];
                lineID  = new Integer( vertex.lineID );
                node    = (YaxisTreeNode) map_line2treenodes.get( lineID );
                nodes   = node.getPath();
                linebuf.append( "time" + description
                              + " = " + fmt.format( vertex.time ) );
                for ( ii = 1; ii < nodes.length; ii++ )
                    linebuf.append( ", " + y_colnames[ ii-1 ]
                                  + " = " + nodes[ ii ] );
                if ( num_cols < linebuf.length() )
                    num_cols = linebuf.length();
                num_rows++;
                strbuf.append( linebuf.toString() );
                if ( idx < coords_length-1 )
                    strbuf.append( "\n" );
            }
        }
    
        private void setEndCoordsText( final Coord     start_vtx,
                                       final Coord     final_vtx,
                                             double    earliest_time,
                                             double    latest_time,
                                             int       coords_length )
        {
            StringBuffer   linebuf;
            Coord          vertex;
            YaxisTreeNode  node;
            TreeNode[]     nodes;
            Integer        lineID;
            double         duration;
            int            idx, ii;
    
            duration = latest_time - earliest_time;
            linebuf = new StringBuffer();
            linebuf.append( "duration (max) = " + tfmt.format( duration ) );
            if ( num_cols < linebuf.length() )
                num_cols = linebuf.length();
            num_rows++;
            strbuf.append( linebuf.toString() );
    
            idx     = 0;
            linebuf = new StringBuffer( "[" + idx + "]: " );
            vertex  = start_vtx;
            lineID  = new Integer( vertex.lineID );
            node    = (YaxisTreeNode) map_line2treenodes.get( lineID );
            nodes   = node.getPath();
            linebuf.append( "time (min) = " + fmt.format( earliest_time ) );
            for ( ii = 1; ii < nodes.length; ii++ )
                linebuf.append( ", " + y_colnames[ ii-1 ]
                              + " = " + nodes[ ii ] );
            if ( num_cols < linebuf.length() )
                num_cols = linebuf.length();
            num_rows++;
            strbuf.append( "\n" + linebuf.toString() );
    
            idx     = coords_length-1;
            linebuf = new StringBuffer( "[" + idx + "]: " );
            vertex  = final_vtx;
            lineID  = new Integer( vertex.lineID );
            node    = (YaxisTreeNode) map_line2treenodes.get( lineID );
            nodes   = node.getPath();
            linebuf.append( "time (max) = " + fmt.format( latest_time ) );
            for ( ii = 1; ii < nodes.length; ii++ )
                linebuf.append( ", " + y_colnames[ ii-1 ]
                              + " = " + nodes[ ii ] );
            if ( num_cols < linebuf.length() )
                num_cols = linebuf.length();
            num_rows++;
            strbuf.append( "\n" + linebuf.toString() );
        }
    
        // For Real Primitive
        public void setPrimitiveText( final Primitive prime )
        {
            this.setCoordsText( prime.getVertices(), "" );
    
            String   info_str;
            info_str = prime.toInfoBoxString().trim();
            if ( info_str.length() > 0 ) {
                strbuf.append( "\n" + info_str );
                num_rows++;
            }
    
            Drawable prime_parent;
            prime_parent = prime.getParent();
            if ( prime_parent != null ) {
                info_str = prime_parent.toInfoBoxString().trim();
                if ( info_str.length() > 0 ) {
                    strbuf.append( "\n" + info_str );
                    num_rows++;
                }
            }
        }

        private int  getPrintStatus( final Topology topo )
        {
           if ( topo.isState() )
                if (    Parameters.PREVIEW_STATE_DISPLAY.equals(
                        PreviewState.CUMULATIVE_EXCLUSION )
                     || Parameters.PREVIEW_STATE_DISPLAY.equals(
                        PreviewState.OVERLAP_EXCLUSION ) )
                    return CategoryWeight.PRINT_EXCL_RATIO;
                else
                    return CategoryWeight.PRINT_INCL_RATIO;
            else // if ( topo.isArrow() )
                return CategoryWeight.PRINT_INCL_RATIO;
        }
    
        // For Shadow Primitive
        public void setShadowText( final Shadow    shade,
                                   final Category  type )
        {
            this.setEndCoordsText( shade.getStartVertex(),
                                   shade.getFinalVertex(),
                                   shade.getEarliestTime(),
                                   shade.getLatestTime(),
                                   shade.getVertices().length );
            strbuf.append( "\n\n" );
            this.setCoordsText( shade.getVertices(), " (ave)" );
            strbuf.append( "\n" );
    
            StringBuffer      linebuf;
            Topology          shade_topo;
            CategoryWeight[]  twgts;
            CategoryWeight    twgt;
            String            twgt_str;
            int               print_status;
            int               idx;

            shade_topo  = shade.getCategory().getTopology();

            // linebuf = new StringBuffer( "Number of Real Drawables = " );
            linebuf = new StringBuffer( "Number of Real " );
            linebuf.append( shade_topo + "s = " );
            linebuf.append( shade.getNumOfRealObjects() );
            if ( num_cols < linebuf.length() )
                num_cols = linebuf.length();
            num_rows++;
            strbuf.append( "\n" + linebuf.toString() );
            strbuf.append( "\n" );
    
            print_status = getPrintStatus( shade_topo );
            strbuf.append( "\n" + CategoryWeight.getPrintTitle(print_status) );
            twgts = shade.arrayOfCategoryWeights();
            for ( idx = 0; idx < twgts.length; idx++ ) {
                twgt     = twgts[ idx ];
                twgt_str = twgt.toInfoBoxString( print_status );
                if ( twgt.getCategory().equals( type ) ) {
                    twgt_str += "  <---";
                    if ( num_cols < twgt_str.length() + 6 )
                        num_cols = twgt_str.length() + 6;
                }
                else {
                    if ( num_cols < twgt_str.length() )
                        num_cols = twgt_str.length();
                }
                num_rows++;
                strbuf.append( "\n" + twgt_str );
            }                               
        }
    
        // For Real Composite
        public void setCompositeText( final Composite cmplx )
        {
            Coord[]  cmplx_coords;
            cmplx_coords = new Coord[] { cmplx.getStartVertex(),
                                         cmplx.getFinalVertex() };
            this.setCoordsText( cmplx_coords, "" );
    
            String   info_str;
            info_str = cmplx.toInfoBoxString().trim();
            if ( info_str.length() > 0 ) {
                strbuf.append( "\n" + info_str );
                num_rows++;
            }
    
            Drawable cmplx_parent;
            cmplx_parent = cmplx.getParent();
            if ( cmplx_parent != null ) {
                info_str = cmplx_parent.toInfoBoxString().trim();
                if ( info_str.length() > 0 ) {
                    strbuf.append( "\n" + info_str );
                    num_rows++;
                }
            }
        }
    }   //  End of   private class TextAreaBuffer
}

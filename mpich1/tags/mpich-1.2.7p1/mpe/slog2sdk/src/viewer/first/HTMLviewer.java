/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.first;

import java.io.*;
import java.net.*;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.text.*;
import javax.swing.event.*;
import java.util.Stack;

import viewer.common.Const;
import viewer.common.Dialogs;
import viewer.common.TopWindow;

public class HTMLviewer extends JDialog
                        implements HyperlinkListener
{
    private JTextField   input_fld;
    private JEditorPane  html_panel;

    private JButton      init_btn;

    private JButton      backward_btn;
    private JButton      forward_btn;
    private JButton      refresh_btn;
    private JButton      close_btn;
    private Stack        url_undo_stack;
    private Stack        url_redo_stack;

    public HTMLviewer( String title_str )
    {
        super( TopWindow.First.getWindow() );
        if ( title_str != null )
            setTitle( title_str );
        else
            setTitle( "HTML viewer" );
        super.setSize( 600, 400 );
        super.setBackground( Color.gray );
        super.getContentPane().setLayout( new BorderLayout() );

        url_undo_stack = new Stack();
        url_redo_stack = new Stack();

        super.getContentPane().add( createToolBar(), BorderLayout.NORTH );

        int         fld_height, fld_width;
        Dimension   min_size, max_size, pref_size;
        fld_width    = 400;
        fld_height   = 25;
        min_size     = new Dimension( 0, fld_height );
        max_size     = new Dimension( Short.MAX_VALUE, fld_height );
        pref_size    = new Dimension( fld_width, fld_height );

        JPanel center_panel = new JPanel();
        center_panel.setLayout( new BoxLayout( center_panel,
                                               BoxLayout.Y_AXIS ) );
            JPanel top_panel = new JPanel();
            top_panel.setLayout( new BoxLayout( top_panel,
                                                BoxLayout.X_AXIS ) );
                JLabel URL_label = new JLabel( "    URL : " );
            top_panel.add( URL_label ); 
                input_fld  = new JTextField();
                input_fld.setMinimumSize( min_size );
                input_fld.setMaximumSize( max_size );
                input_fld.setPreferredSize( pref_size );
            top_panel.add( input_fld );
       center_panel.add( top_panel );

            JScrollPane scroll_panel = new JScrollPane();
            scroll_panel.setBorder( BorderFactory.createLoweredBevelBorder() );
                html_panel = new JEditorPane();
                html_panel.setEditable( false );
            scroll_panel.getViewport().add( html_panel );
        center_panel.add( scroll_panel );
        super.getContentPane().add( center_panel, BorderLayout.CENTER );

        input_fld.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                refresh_btn.doClick();
            }
        } );
        html_panel.addHyperlinkListener( this );

        init_btn = null;

        addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent evt ) {
                HTMLviewer.this.setVisible( false );
            }
        } );
    }

    public HTMLviewer( String title_str, JButton button )
    {
        this( title_str );
        init_btn = button;
    }

    // Overriden setVisible to enable/disable the init_btn
    public void setVisible( boolean val )
    {
        super.setVisible( val );
        if ( init_btn != null )
            init_btn.setEnabled( !val );
    }

    public void init( URL init_URL )
    {
        URL curr_URL = init_URL;
        // System.out.println( "curr_URL = " + curr_URL );

        if ( curr_URL != null ) {
            input_fld.setText( curr_URL.toString() ); 
            refresh_btn.doClick();
        }    
    }

    protected URL getURL( String filename )
    {
        URL url = null;
        url = getClass().getResource( filename );
        return url;
    }

    private JToolBar createToolBar()
    {
        JToolBar     toolbar;
        Insets       btn_insets;
        Dimension    small_spacer_size, medium_spacer_size, big_spacer_size;
        URL          icon_URL;


        toolbar             = new JToolBar();
        btn_insets          = new Insets( 1, 1, 1, 1 );
        small_spacer_size   = new Dimension( 5, 5 );
        medium_spacer_size  = new Dimension( 10, 5 );
        big_spacer_size     = new Dimension( 20, 5 );

        icon_URL = getURL( Const.IMG_PATH + "Backward24.gif" );
        if ( icon_URL != null )
            backward_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            backward_btn = new JButton( "Backward" );
        backward_btn.setMargin( btn_insets );
        backward_btn.setToolTipText( "Go Backward one page" );
        // backward_btn.setPreferredSize( btn_dim );
        backward_btn.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                if ( ! url_undo_stack.empty() ) {
                    updateURLStack( url_redo_stack );
                    URL url = (URL) url_undo_stack.pop();
                    input_fld.setText( url.toString() );
                    refresh_btn.doClick();
                }
            }
        } );
        toolbar.add( backward_btn );

        toolbar.addSeparator( small_spacer_size );

        icon_URL = getURL( Const.IMG_PATH + "Forward24.gif" );
        if ( icon_URL != null )
            forward_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            forward_btn = new JButton( "Forward" );
        forward_btn.setMargin( btn_insets );
        forward_btn.setToolTipText( "Go Forward one page" );
        // forward_btn.setPreferredSize( btn_dim );
        forward_btn.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                if ( ! url_redo_stack.empty() ) {
                    updateURLStack( url_undo_stack );
                    URL url = (URL) url_redo_stack.pop();
                    input_fld.setText( url.toString() );
                    refresh_btn.doClick();
                }
            }
        } );
        toolbar.add( forward_btn );

        toolbar.addSeparator( medium_spacer_size );

        icon_URL = getURL( Const.IMG_PATH + "Refresh24.gif" );
        if ( icon_URL != null )
            refresh_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            refresh_btn = new JButton( "Refresh" );
        refresh_btn.setMargin( btn_insets );
        refresh_btn.setToolTipText( "Refresh the current page" );
        // refresh_btn.setPreferredSize( btn_dim );
        refresh_btn.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                URL curr_URL = null;
                try {
                    curr_URL = new URL( input_fld.getText() );
                    html_panel.setPage( curr_URL );
                } catch ( IOException ioerr ) {
                    Dialogs.error( HTMLviewer.this,
                                   "Invalid URL: " + curr_URL.toString() );
                }
            }
        } );
        toolbar.add( refresh_btn );

        toolbar.addSeparator( big_spacer_size );

        icon_URL = getURL( Const.IMG_PATH + "Stop24.gif" );
        if ( icon_URL != null )
            close_btn = new JButton( new ImageIcon( icon_URL ) );
        else
            close_btn = new JButton( "Close" );
        close_btn.setMargin( btn_insets );
        close_btn.setToolTipText( "Close the HTMLviewer window" );
        // close_btn.setPreferredSize( btn_dim );
        close_btn.addActionListener( new ActionListener() {
            public void actionPerformed( ActionEvent evt ) {
                HTMLviewer.this.setVisible( false );
            }
        } );
        toolbar.add( close_btn );

        return toolbar;
    }

    private void updateURLStack( Stack url_stack )
    {
        String url_str = input_fld.getText();
        if ( url_str != null ) {
            try {
                URL url = new URL( url_str );
                url_stack.push( url );
            } catch ( java.net.MalformedURLException ioerr ) {
                Dialogs.error( this, "Malformed URL " + url_str );
            }
        }
    }

    public void hyperlinkUpdate( final HyperlinkEvent evt )
    {
        if ( evt.getEventType() == HyperlinkEvent.EventType.ACTIVATED ) {
            html_panel.setCursor( new Cursor( Cursor.WAIT_CURSOR ) );
            SwingUtilities.invokeLater( new Runnable() {
                public void run() {
                    Document  docu     = html_panel.getDocument();
                    URL       curr_URL = null;
                    try {
                        curr_URL   = evt.getURL();
                        if ( curr_URL != null ) {
                            updateURLStack( url_undo_stack );
                            // System.out.println( "  curr_URL = " + curr_URL );
                            input_fld.setText( curr_URL.toString() );
                            html_panel.setPage( curr_URL );
                        }
                        else {
                            Dialogs.error( HTMLviewer.this,
                                           "Invalid Link: NULL pointer!" ); 
                            html_panel.setDocument( docu );
                        }
                    } catch ( IOException ioerr ) {
                        Dialogs.error( HTMLviewer.this,
                                       "Invalid Link: " + curr_URL.toString() );
                    }
                    html_panel.setCursor( new Cursor( Cursor.DEFAULT_CURSOR ) );
                }
            } );
        }
    } 

    public static void main( String args[] )
    {
        URL init_URL = ClassLoader.getSystemResource( "doc/html/index.html" );
        HTMLviewer htmlview = new HTMLviewer( null );
        htmlview.init( init_URL );
        htmlview.setVisible( true );
    }
}

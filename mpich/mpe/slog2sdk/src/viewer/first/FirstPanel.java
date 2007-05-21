/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.first;

import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.*;
import javax.swing.border.Border;
import java.net.URL;
import java.util.List;
import java.util.Iterator;

import logformat.slog2.LineIDMap;
import viewer.common.Const;
import viewer.common.Routines;
import viewer.common.TopWindow;
import viewer.common.Dialogs;
import viewer.common.ActableTextField;



public class FirstPanel extends JPanel
{
    private static String        about_str = "Jumpshot-4, the SLOG-2 viewer.\n"
                                           + "bug-reports/questions:\n"
                                           + "            chan@mcs.anl.gov";
    private static String        manual_path        = Const.DOC_PATH
                                                    + "usersguide.html";
    private static String        faq_path           = Const.DOC_PATH
                                                    + "faq_index.html";
    private static String        js_icon_path       = Const.IMG_PATH
                                                    + "jumpshot.gif";

    private static String        open_icon_path     = Const.IMG_PATH
                                                    + "Open24.gif";
    private static String        convert_icon_path  = Const.IMG_PATH
                                                    + "Convert24.gif";
    private static String        show_icon_path     = Const.IMG_PATH
                                                    + "New24.gif";
    private static String        close_icon_path    = Const.IMG_PATH
                                                    + "Stop24.gif";
    private static String        legend_icon_path   = Const.IMG_PATH
                                                    + "Properties24.gif";
    private static String        prefer_icon_path   = Const.IMG_PATH
                                                    + "Preferences24.gif";
    private static String        manual_icon_path   = Const.IMG_PATH
                                                    + "Help24.gif";
    private static String        faq_icon_path      = Const.IMG_PATH
                                                    + "Information24.gif";
    private static String        about_icon_path    = Const.IMG_PATH
                                                    + "About24.gif";

    private        ActableTextField    logname_fld;
    private        JComboBox           pulldown_list;

    /*  some of these are hidden buttons */
    private        JButton             file_select_btn;
    private        JButton             file_convert_btn;
    private        JButton             file_close_btn;
    private        JButton             show_timeline_btn;
    private        JButton             show_legend_btn;
    private        JButton             edit_prefer_btn;
    private        JButton             help_manual_btn;
    private        JButton             help_faq_btn;
    private        JButton             help_about_btn;

    private        HTMLviewer          manual_viewer;
    private        HTMLviewer          faq_viewer;

    private        LogFileOperations   file_ops;
    private        String              logfile_name;
    private        int                 view_ID;


    public FirstPanel( boolean isApplet, String filename, int view_idx )
    {
        super();
        super.setLayout( new BorderLayout() );

        Border   lowered_border, etched_border;
        lowered_border  = BorderFactory.createLoweredBevelBorder();
        etched_border   = BorderFactory.createEtchedBorder();

        file_ops     = new LogFileOperations( isApplet );
        logfile_name = filename;
        view_ID      = view_idx;


        Dimension   row_pref_sz;
        Dimension   lbl_pref_sz;
        Dimension   fld_pref_sz;
        row_pref_sz  = new Dimension( 410, 27 );
        lbl_pref_sz  = new Dimension( 110, 25 );
        fld_pref_sz  = new Dimension( row_pref_sz.width - lbl_pref_sz.width,
                                      lbl_pref_sz.height );

        JPanel  ctr_panel;
        ctr_panel  = new JPanel();
        ctr_panel.setLayout( new BoxLayout( ctr_panel, BoxLayout.Y_AXIS ) );
            ctr_panel.add( Box.createVerticalGlue() );

                JLabel  label;
                JPanel logname_panel = new JPanel();
                logname_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
                logname_panel.setLayout( new BoxLayout( logname_panel,
                                                        BoxLayout.X_AXIS ) );
    
                    label = new JLabel( " LogName : " );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                logname_panel.add( label );
                    logname_fld = new ActableTextField( logfile_name, 40 );
                    logname_fld.setBorder( BorderFactory.createCompoundBorder(
                                           lowered_border, etched_border ) );
                    logname_fld.addActionListener(
                                new LogNameTextFieldListener() );
                    Routines.setShortJComponentSizes( logname_fld,
                                                      fld_pref_sz );
                logname_panel.add( logname_fld );
                Routines.setShortJComponentSizes( logname_panel, row_pref_sz );
                // logname_panel.add( Box.createHorizontalStrut( 40 ) );
            ctr_panel.add( logname_panel );
            ctr_panel.add( Box.createVerticalGlue() );
            ctr_panel.add( Box.createVerticalStrut( 4 ) );
    
                JPanel map_panel = new JPanel();
                map_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
                map_panel.setLayout( new BoxLayout( map_panel,
                                                    BoxLayout.X_AXIS ) );
    
                    label = new JLabel( " ViewMap : " );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                map_panel.add( label );
                    pulldown_list = new JComboBox();
                    pulldown_list.setBorder( lowered_border );
                    pulldown_list.addActionListener(
                                  new ViewMapComboBoxListener() );
                    Routines.setShortJComponentSizes( pulldown_list,
                                                      fld_pref_sz );
                    /*
                    //  ItemListener does not work here, because the listener
                    //  is not invoked when same item is selected again.
                    pulldown_list.addItemListener( new ItemListener() {
                        public void itemStateChanged( ItemEvent evt ) {
                            if ( evt.getStateChange() == ItemEvent.SELECTED ) {
                                view_ID = pulldown_list.getSelectedIndex();
                                file_ops.createTimelineWindow( view_ID );
                            }
                        }
                    } );
                    */
                map_panel.add( pulldown_list );
                Routines.setShortJComponentSizes( map_panel, row_pref_sz );
    
            ctr_panel.add( map_panel );
            ctr_panel.add( Box.createVerticalGlue() );

        ctr_panel.setBorder( etched_border );
        super.add( ctr_panel, BorderLayout.CENTER );

        JToolBar  toolbar;
        toolbar  = createToolBarAndButtons( JToolBar.HORIZONTAL );
        super.add( toolbar, BorderLayout.SOUTH );
    }

    private JToolBar createToolBarAndButtons( int orientation )
    {
        Border    raised_border, empty_border;
        raised_border   = BorderFactory.createRaisedBevelBorder();
        empty_border    = BorderFactory.createEmptyBorder();

        JToolBar  toolbar;
        toolbar         = new JToolBar( orientation );
        toolbar.setFloatable( true );

        Insets    btn_insets;
        btn_insets      = new Insets( 1, 1, 1, 1 );

        URL     icon_URL;

            icon_URL = null;
            icon_URL = getURL( open_icon_path );
            if ( icon_URL != null )
                file_select_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                file_select_btn = new JButton( "SELECT" );
            file_select_btn.setToolTipText( "Select a new logfile" );
            // file_select_btn.setBorder( empty_border );
            file_select_btn.setMargin( btn_insets );
            file_select_btn.addActionListener(
                            new FileSelectButtonListener() );
        toolbar.add( file_select_btn );

            icon_URL = null;
            icon_URL = getURL( convert_icon_path );
            if ( icon_URL != null )
                file_convert_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                file_convert_btn = new JButton( "CONVERT" );
            file_convert_btn.setToolTipText( "Invoke the Logfile Convertor" );
            // file_convert_btn.setBorder( empty_border );
            file_convert_btn.setMargin( btn_insets );
            file_convert_btn.addActionListener(
                             new FileConvertButtonListener() );
        toolbar.add( file_convert_btn );

        toolbar.addSeparator();

            icon_URL = null;
            icon_URL = getURL( legend_icon_path );
            if ( icon_URL != null )
                show_legend_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                show_legend_btn = new JButton( "LEGEND" );
            show_legend_btn.setToolTipText( "Display the Legend window" );
            // show_legend_btn.setBorder( empty_border );
            show_legend_btn.setMargin( btn_insets );
            show_legend_btn.addActionListener( 
                            new ShowLegendButtonListener() );
        toolbar.add( show_legend_btn );

            icon_URL = null;
            icon_URL = getURL( show_icon_path );
            if ( icon_URL != null )
                show_timeline_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                show_timeline_btn = new JButton( "TIMELINE" );
            show_timeline_btn.setToolTipText( "Display the Timeline window" );
            // show_timeline_btn.setBorder( empty_border );
            show_timeline_btn.setMargin( btn_insets );
            show_timeline_btn.addActionListener(
                              new ViewMapComboBoxListener() );
        toolbar.add( show_timeline_btn );

        toolbar.addSeparator();

            icon_URL = null;
            icon_URL = getURL( prefer_icon_path );
            if ( icon_URL != null )
                edit_prefer_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                edit_prefer_btn = new JButton( "PREFERENCE" );
            edit_prefer_btn.setToolTipText( "Open the Preference window" );
            // edit_prefer_btn.setBorder( empty_border );
            edit_prefer_btn.setMargin( btn_insets );
            edit_prefer_btn.addActionListener(
                            new EditPreferButtonListener() );
        toolbar.add( edit_prefer_btn );

        toolbar.addSeparator();

            icon_URL = null;
            icon_URL = getURL( manual_icon_path );
            if ( icon_URL != null )
                help_manual_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                help_manual_btn = new JButton( "MANUAL" );
            help_manual_btn.setToolTipText( "Open the user's manual window" );
            // help_manual_btn.setBorder( empty_border );
            help_manual_btn.setMargin( btn_insets );
            help_manual_btn.addActionListener(
                            new HelpManualButtonListener() );
        toolbar.add( help_manual_btn );

            icon_URL = null;
            icon_URL = getURL( faq_icon_path );
            if ( icon_URL != null )
                help_faq_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                help_faq_btn = new JButton( "FAQ" );
            help_faq_btn.setToolTipText( "Open the FAQ window" );
            // help_faq_btn.setBorder( empty_border );
            help_faq_btn.setMargin( btn_insets );
            help_faq_btn.addActionListener(
                         new HelpFAQsButtonListener() );
        toolbar.add( help_faq_btn );

            /* help_about_btn is a hidden button */
            icon_URL = null;
            icon_URL = getURL( about_icon_path );
            if ( icon_URL != null )
                help_about_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                help_about_btn = new JButton( "ABOUT" );
            help_about_btn.setToolTipText( "Open the About-This window" );
            // help_about_btn.setBorder( empty_border );
            help_about_btn.setMargin( btn_insets );
            help_about_btn.addActionListener(
                           new HelpAboutButtonListener() );

            /* file_close_btn is a hidden button */
            icon_URL = null;
            icon_URL = getURL( close_icon_path );
            if ( icon_URL != null )
                file_close_btn = new JButton( new ImageIcon( icon_URL ) );
            else
                file_close_btn = new JButton( "CLOSE" );
            file_close_btn.setToolTipText( "Close the logfile" );
            // file_close_btn.setBorder( empty_border );
            file_close_btn.setMargin( btn_insets );
            file_close_btn.addActionListener(
                           new FileCloseButtonListener() );

            manual_viewer = new HTMLviewer( "Manual", help_manual_btn );
            faq_viewer    = new HTMLviewer( "FAQs", help_faq_btn );

        return toolbar;
    }

    public void init()
    {
        file_ops.init();
        if ( logfile_name != null )
            logname_fld.fireActionPerformed();
    }

    private URL getURL( String filename )
    {
        return getClass().getResource( filename );
    }

    private void setMapPullDownMenu( List list )
    {
        pulldown_list.removeAllItems();

        String map_title;
        Iterator linemaps = list.iterator();
        /* Crucial to add LineIDMapList's element in the order it is created */
        while ( linemaps.hasNext() ) {
            map_title = "  " + ( (LineIDMap) linemaps.next() ).getTitle();
            pulldown_list.addItem( map_title );
        }
    }

    public JButton getLogFileSelectButton()
    {
        return file_select_btn;
    }

    public JButton getLogFileConvertButton()
    {
        return file_convert_btn;
    }

    public JButton getLogFileCloseButton()
    {
        return file_close_btn;
    }

    public JButton getShowLegendButton()
    {
        return show_legend_btn;
    }

    public JButton getShowTimelineButton()
    {
        return show_timeline_btn;
    }

    public JButton getEditPreferenceButton()
    {
        return edit_prefer_btn;
    }

    public JButton getHelpManualButton()
    {
        return help_manual_btn;
    }

    public JButton getHelpFAQsButton()
    {
        return help_faq_btn;
    }

    public JButton getHelpAboutButton()
    {
        return help_about_btn;
    }




    private class FileSelectButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            final String filename = file_ops.selectLogFile();
            if ( filename != null && filename.length() > 0 ) {
                logname_fld.setText( filename );
                // invoke LogNameTextFieldListener.actionPerformed()
                logname_fld.fireActionPerformed();
            }
        }
    }

    private class FileConvertButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String filename, new_filename;
            new_filename  = null;
            filename      = logname_fld.getText();
            if ( filename != null && filename.length() > 0 )
                new_filename = file_ops.convertLogFile( filename );
            else
                new_filename = file_ops.convertLogFile( null );
            if ( new_filename != null && new_filename.length() > 0 ) {
                logname_fld.setText( new_filename );
                // invoke LogNameTextFieldListener.actionPerformed()
                logname_fld.fireActionPerformed();
            }
        }
    }

    private class LogNameTextFieldListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            // pulldown_list.removeAllItems();  // done by setMapPullDownMenu()
            file_ops.disposeLogFileAndResources();

            List lineIDmaps = file_ops.openLogFile( logname_fld );
            if ( lineIDmaps != null ) {
                FirstPanel.this.setMapPullDownMenu( lineIDmaps );
                // Timeline window is created by ViewMapComboBoxListener
                // if ( lineIDmaps.size() == 1 )
                //     file_ops.createTimelineWindow( view_ID=0 );
            }
        }
    }

    //  This is essentially ShowTimelineButtonListener
    private class ViewMapComboBoxListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            // System.out.println( "pulldown_list: " + evt );
            view_ID = pulldown_list.getSelectedIndex();
            /*
               JComboBox.removeAllItems() seems to trigger a selected index=-1 
               action event.  So filter only the relevant selection event.
            */
            if ( view_ID >= 0 && view_ID < pulldown_list.getItemCount() ) {
                file_ops.createTimelineWindow( view_ID );
            }
        }
    }

    private class ShowLegendButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            file_ops.showLegendWindow();
        }
    }

    private class EditPreferButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            file_ops.showPreferenceWindow();
        }
    }

    private class HelpManualButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            URL manual_URL = getURL( manual_path );
            if ( manual_URL != null ) {
                manual_viewer.init( manual_URL );
                manual_viewer.setVisible( true );
            }
            else
                Dialogs.warn( TopWindow.First.getWindow(),
                              "Cannot locate " + manual_path + "." );
        }
    }

    private class HelpFAQsButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
           URL faq_URL = getURL( faq_path );
            if ( faq_URL != null ) { 
                faq_viewer.init( faq_URL );
                faq_viewer.setVisible( true );
            }
            else
                Dialogs.warn( TopWindow.First.getWindow(),
                              "Cannot locate " + faq_path + "." );
        }
    }

    private class HelpAboutButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            URL icon_URL = getURL( js_icon_path );
            if ( icon_URL != null ) {
                ImageIcon js_icon = new ImageIcon( icon_URL );
                Dialogs.info( TopWindow.First.getWindow(), about_str, js_icon );
            }
            else
                Dialogs.info( TopWindow.First.getWindow(), about_str, null );
        }
    }

    private class FileCloseButtonListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            file_ops.disposeLogFileAndResources();
            pulldown_list.removeAllItems();
        }
    }

}

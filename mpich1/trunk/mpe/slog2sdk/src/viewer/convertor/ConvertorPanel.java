/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.awt.Color;
import java.awt.Insets;
import java.awt.Dimension;
import java.awt.Component;
import java.awt.Window;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JPanel;
import javax.swing.JLabel;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JTextField;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JProgressBar;
import javax.swing.ImageIcon;
import javax.swing.AbstractButton;
import javax.swing.BorderFactory;
import javax.swing.border.Border;
import javax.swing.border.TitledBorder;
import javax.swing.SwingUtilities;
import javax.swing.UIManager;
import java.util.Properties;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.IOException;
import java.net.URL;

import logformat.slog2.input.InputLog;
import viewer.common.Const;
import viewer.common.Dialogs;
import viewer.common.Routines;
import viewer.common.CustomCursor;
import viewer.common.ActableTextField;
import viewer.common.LogFileChooser;
import viewer.common.RuntimeExecCommand;

public class ConvertorPanel extends JPanel
                            implements WaitingContainer
{
    private JComboBox          cmd_pulldown;
    private ActableTextField   cmd_infile;
    private JButton            infile_btn;
    private JTextField         cmd_outfile;
    private JButton            outfile_btn;
    private AdvancingTextArea  cmd_textarea;
    private JTextField         cmd_outfile_size;
    private JProgressBar       cmd_progress;
    private JTextField         cmd_option4jvm;
    private JTextField         cmd_option4jar;
    private JTextField         cmd_path2jvm;
    private ActableTextField   cmd_path2jardir;
    private JTextField         cmd_path2tracelib;
    private JSplitPane         cmd_splitter;
    private JButton            cmd_start_btn;
    private JButton            cmd_stop_btn;
    private JButton            cmd_help_btn;
    private JButton            cmd_close4ok_btn;
    private JButton            cmd_close4cancel_btn;

    private Window             top_window;
    private LogFileChooser     file_chooser;
    private String             file_sep, path_sep;
    private String             err_msg;

    private SwingProcessWorker logconv_worker;

    public ConvertorPanel( LogFileChooser  in_file_chooser )
    {
        super();
        this.initComponents( in_file_chooser != null );
        this.initAllTextFields();

        if ( in_file_chooser != null )
            file_chooser  = in_file_chooser;
        else
            file_chooser  = new LogFileChooser( false );

        cmd_pulldown.addActionListener( new PulldownListener() );
        cmd_infile.addActionListener( new LogNameListener() );
        infile_btn.addActionListener( new InputFileSelectorListener() );
        outfile_btn.addActionListener( new OutputFileSelectorListener() );

        cmd_path2jardir.addActionListener( new JarDirectoryListener() );

        cmd_start_btn.addActionListener( new StartConvertorListener() );
        cmd_stop_btn.addActionListener( new StopConvertorListener() );
        cmd_help_btn.addActionListener( new HelpConvertorListener() );
        this.finalizeWaiting();

        logconv_worker = null;
    }

    public void init( String filename )
    {
        top_window = SwingUtilities.windowForComponent( this );
        cmd_splitter.setDividerLocation( 1.0d );
        if ( filename != null && filename.length() > 0 ) {
            cmd_infile.setText( filename );
            cmd_infile.fireActionPerformed(); // Invoke LogNameListener()
            cmd_pulldown.setSelectedItem(
                         ConvertorConst.getDefaultConvertor( filename ) );
            if ( cmd_close4ok_btn != null )
                cmd_close4ok_btn.setEnabled( false );
            if ( cmd_close4cancel_btn != null )
                cmd_close4cancel_btn.setEnabled( true );
        }
        if ( err_msg != null )
            Dialogs.error( top_window, err_msg );
    }



    private URL getURL( String filename )
    {
        URL url = null;
        url = getClass().getResource( filename );
        return url;
    }

    private void initComponents( boolean has_close4ok_btn )
    {
        Border   raised_border, etched_border;
        raised_border  = BorderFactory.createRaisedBevelBorder();
        etched_border  = BorderFactory.createEtchedBorder();

        //  Setup all relevant Dimension of various components
        Dimension   row_pref_sz;   // for typical row JPanel
        Dimension   lbl_pref_sz;   // for all JLabel
        Dimension   fld_pref_sz;   // for all JTextField
        Dimension   pfld_pref_sz;  // for JProgressBar
        Dimension   pbar_pref_sz;  // for JTextField of Output File Size
        row_pref_sz  = new Dimension( 410, 30 );
        lbl_pref_sz  = new Dimension( 130, 26 );
        fld_pref_sz  = new Dimension( row_pref_sz.width - lbl_pref_sz.width,
                                      lbl_pref_sz.height );
        pfld_pref_sz = new Dimension( lbl_pref_sz.width,
                                      2 * lbl_pref_sz.height );
        pbar_pref_sz = new Dimension( row_pref_sz.width,
                                      pfld_pref_sz.height );

        super.setLayout( new BoxLayout( this, BoxLayout.Y_AXIS ) );

            Color thumb_color, pulldown_bg_color;
            thumb_color = UIManager.getColor( "ScrollBar.thumb" );
            pulldown_bg_color = Routines.getSlightBrighterColor( thumb_color );

            JLabel  label;
            Insets  btn_insets;
            URL     icon_URL;

            JPanel  upper_panel = new JPanel();
            upper_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
            upper_panel.setLayout( new BoxLayout( upper_panel,
                                                  BoxLayout.Y_AXIS ) );
            upper_panel.add( Box.createVerticalStrut( 4 ) );

                JPanel  cmd_name_panel = new JPanel();
                cmd_name_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
                cmd_name_panel.setLayout( new BoxLayout( cmd_name_panel,
                                                         BoxLayout.X_AXIS ) );
                cmd_name_panel.add( Box.createHorizontalStrut( 5 ) );

                    cmd_pulldown = new JComboBox();
                    cmd_pulldown.setForeground( Color.yellow );
                    cmd_pulldown.setBackground( pulldown_bg_color );
                    cmd_pulldown.setToolTipText( " Logfile Convertor's Name " );
                    cmd_pulldown.addItem( ConvertorConst.CLOG2_TO_SLOG2 );
                    cmd_pulldown.addItem( ConvertorConst.CLOG_TO_SLOG2 );
                    cmd_pulldown.addItem( ConvertorConst.RLOG_TO_SLOG2 );
                    cmd_pulldown.addItem( ConvertorConst.UTE_TO_SLOG2 );
                    cmd_pulldown.setBorder( raised_border );
                    cmd_pulldown.setEditable( false );
                    cmd_pulldown.setAlignmentX( Component.CENTER_ALIGNMENT );
                cmd_name_panel.add( cmd_pulldown );
                cmd_name_panel.add( Box.createHorizontalStrut( 5 ) );
                Routines.setShortJComponentSizes( cmd_name_panel,
                                                  row_pref_sz );

            upper_panel.add( cmd_name_panel );
            upper_panel.add( Box.createVerticalStrut( 4 ) );

                btn_insets      = new Insets( 1, 1, 1, 1 );

                JPanel  cmd_infile_panel = new JPanel();
                cmd_infile_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
                cmd_infile_panel.setLayout( new BoxLayout( cmd_infile_panel,
                                                           BoxLayout.X_AXIS ) );

                    label = new JLabel( " Input File Spec. : " );
                    label.setToolTipText(
                          "File Specification of the Input Trace File." );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_infile_panel.add( label );
                    cmd_infile = new ActableTextField();
                cmd_infile_panel.add( cmd_infile );
                    icon_URL = getURL( Const.IMG_PATH + "Open24.gif" );
                    infile_btn = null;
                    if ( icon_URL != null )
                        infile_btn = new JButton( new ImageIcon( icon_URL ) );
                    else
                        infile_btn = new JButton( "Browse" );
                    infile_btn.setToolTipText( "Select a new Input Logfile" );
                    infile_btn.setMargin( btn_insets );
                cmd_infile_panel.add( infile_btn );
                Routines.setShortJComponentSizes( cmd_infile_panel,
                                                  row_pref_sz );

            upper_panel.add( cmd_infile_panel );
            upper_panel.add( Box.createVerticalStrut( 4 ) );

                JPanel  cmd_outfile_panel = new JPanel();
                cmd_outfile_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
                cmd_outfile_panel.setLayout( new BoxLayout( cmd_outfile_panel,
                                                           BoxLayout.X_AXIS ) );

                    label = new JLabel( " Output File Name : " );
                    label.setToolTipText( "File Name of the SLOG-2 File" );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_outfile_panel.add( label );
                    cmd_outfile = new JTextField();
                cmd_outfile_panel.add( cmd_outfile );
                    icon_URL = getURL( Const.IMG_PATH + "Open24.gif" );
                    outfile_btn = null;
                    if ( icon_URL != null )
                        outfile_btn = new JButton( new ImageIcon( icon_URL ) );
                    else
                        outfile_btn = new JButton( "Browse" );
                    outfile_btn.setToolTipText( "Select a new Output Logfile" );
                    outfile_btn.setMargin( btn_insets );
                cmd_outfile_panel.add( outfile_btn );
                Routines.setShortJComponentSizes( cmd_outfile_panel,
                                                  row_pref_sz );

            upper_panel.add( cmd_outfile_panel );
            upper_panel.add( Box.createVerticalStrut( 4 ) );

                    cmd_textarea = new AdvancingTextArea();
                    cmd_textarea.setColumns( 50 );
                    cmd_textarea.setRows( 5 );
                    cmd_textarea.setEditable( false );
                    cmd_textarea.setLineWrap( false );
                JScrollPane scroller = new JScrollPane( cmd_textarea );
                scroller.setAlignmentX( Component.CENTER_ALIGNMENT );

            upper_panel.add( scroller );
            upper_panel.add( Box.createVerticalStrut( 4 ) );

                JPanel cmd_outfile_status_panel = new JPanel();
                cmd_outfile_status_panel.setAlignmentX(
                                         Component.CENTER_ALIGNMENT );
                cmd_outfile_status_panel.setLayout(
                                   new BoxLayout( cmd_outfile_status_panel,
                                                  BoxLayout.X_AXIS ) );

                    JPanel cmd_outfile_size_panel = new JPanel();
                    cmd_outfile_size_panel.setAlignmentY(
                                       Component.CENTER_ALIGNMENT );
                    cmd_outfile_size_panel.setLayout(
                                     new BoxLayout( cmd_outfile_size_panel,
                                                    BoxLayout.X_AXIS ) );
                    cmd_outfile_size_panel.setBorder(
                        new TitledBorder( etched_border,
                                          " Output File Size ") );
                        cmd_outfile_size = new JTextField();
                        cmd_outfile_size.setEditable( false );
                    cmd_outfile_size_panel.add( cmd_outfile_size );
                    Routines.setShortJComponentSizes( cmd_outfile_size_panel,
                                                      pfld_pref_sz );

                cmd_outfile_status_panel.add( cmd_outfile_size_panel );

                    JPanel cmd_progress_panel = new JPanel();
                    cmd_progress_panel.setAlignmentY(
                                       Component.CENTER_ALIGNMENT );
                    cmd_progress_panel.setLayout(
                                       new BoxLayout( cmd_progress_panel,
                                                      BoxLayout.X_AXIS ) );
                    cmd_progress_panel.setBorder(
                        new TitledBorder( etched_border,
                            " Output to Input Logfile Size Ratio " ) );
                        cmd_progress = new JProgressBar();
                        cmd_progress.setStringPainted( true );
                    cmd_progress_panel.add( cmd_progress );
                    Routines.setShortJComponentSizes( cmd_progress_panel,
                                                      pbar_pref_sz );

                cmd_outfile_status_panel.add( cmd_progress_panel );

            upper_panel.add( cmd_outfile_status_panel );



        row_pref_sz  = new Dimension( 410, 27 );
        lbl_pref_sz  = new Dimension( 130, 25 );
        fld_pref_sz  = new Dimension( row_pref_sz.width - lbl_pref_sz.width,
                                      lbl_pref_sz.height );

            JPanel  lower_panel = new JPanel();
            lower_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
            lower_panel.setLayout( new BoxLayout( lower_panel,
                                                  BoxLayout.Y_AXIS ) );
            lower_panel.add( Box.createVerticalStrut( 4 ) );

                JPanel  cmd_path2jvm_panel = new JPanel();
                cmd_path2jvm_panel.setAlignmentX(
                                   Component.CENTER_ALIGNMENT );
                cmd_path2jvm_panel.setLayout(
                                   new BoxLayout( cmd_path2jvm_panel,
                                                  BoxLayout.X_AXIS ) );

                    label = new JLabel( " JVM Path : " );
                    label.setToolTipText(
                          "Full Pathname of the Java Virtual Machine." );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_path2jvm_panel.add( label );
                    cmd_path2jvm = new JTextField();
                    // Routines.setShortJComponentSizes( cmd_path2jvm,
                    //                                   fld_pref_sz );
                cmd_path2jvm_panel.add( cmd_path2jvm );
                Routines.setShortJComponentSizes( cmd_path2jvm_panel,
                                                  row_pref_sz );

            lower_panel.add( cmd_path2jvm_panel );
            lower_panel.add( Box.createVerticalGlue() );
            lower_panel.add( Box.createVerticalStrut( 4 ) );

                JPanel  cmd_option4jvm_panel = new JPanel();
                cmd_option4jvm_panel.setAlignmentX(
                                     Component.CENTER_ALIGNMENT );
                cmd_option4jvm_panel.setLayout(
                                     new BoxLayout( cmd_option4jvm_panel,
                                                    BoxLayout.X_AXIS ) );
    
                    label = new JLabel( " JVM Option : " );
                    label.setToolTipText(
                          "Option to the Java Virtual Machine." );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_option4jvm_panel.add( label );
                    cmd_option4jvm = new JTextField();
                    // Routines.setShortJComponentSizes( cmd_option4jvm,
                    //                                   fld_pref_sz );
                cmd_option4jvm_panel.add( cmd_option4jvm );
                Routines.setShortJComponentSizes( cmd_option4jvm_panel,
                                                  row_pref_sz );
    
            lower_panel.add( cmd_option4jvm_panel );
            lower_panel.add( Box.createVerticalGlue() );
            lower_panel.add( Box.createVerticalStrut( 4 ) );
    
                JPanel  cmd_path2jardir_panel = new JPanel();
                cmd_path2jardir_panel.setAlignmentX(
                                      Component.CENTER_ALIGNMENT );
                cmd_path2jardir_panel.setLayout(
                                      new BoxLayout( cmd_path2jardir_panel,
                                                     BoxLayout.X_AXIS ) );
    
                    label = new JLabel( " JAR Directory : " );
                    label.setToolTipText( "Directory of the .jar files." );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_path2jardir_panel.add( label );
                    cmd_path2jardir = new ActableTextField();
                    // Routines.setShortJComponentSizes( cmd_path2jardir,
                    //                                   fld_pref_sz );
                cmd_path2jardir_panel.add( cmd_path2jardir );
                Routines.setShortJComponentSizes( cmd_path2jardir_panel,
                                                  row_pref_sz );
    
            lower_panel.add( cmd_path2jardir_panel );
            lower_panel.add( Box.createVerticalGlue() );
            lower_panel.add( Box.createVerticalStrut( 4 ) );

                JPanel  cmd_option4jar_panel = new JPanel();
                cmd_option4jar_panel.setAlignmentX(
                                     Component.CENTER_ALIGNMENT );
                cmd_option4jar_panel.setLayout(
                                     new BoxLayout( cmd_option4jar_panel,
                                                    BoxLayout.X_AXIS ) );

                    label = new JLabel( " JAR Option : " );
                    label.setToolTipText( "Option to the selected Convertor." );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_option4jar_panel.add( label );
                    cmd_option4jar = new JTextField();
                    // Routines.setShortJComponentSizes( cmd_option4jar,
                    //                                   fld_pref_sz );
                cmd_option4jar_panel.add( cmd_option4jar );
                Routines.setShortJComponentSizes( cmd_option4jar_panel,
                                                  row_pref_sz );

            lower_panel.add( cmd_option4jar_panel );
            lower_panel.add( Box.createVerticalGlue() );
            lower_panel.add( Box.createVerticalStrut( 4 ) );
    
                JPanel  cmd_path2tracelib_panel = new JPanel();
                cmd_path2tracelib_panel.setAlignmentX(
                                        Component.CENTER_ALIGNMENT );
                cmd_path2tracelib_panel.setLayout(
                                        new BoxLayout( cmd_path2tracelib_panel,
                                                       BoxLayout.X_AXIS ) );
    
                    label = new JLabel( " TraceLibrary Path : " );
                    label.setToolTipText(
                        "Trace Input Library path of the selected Convertor" );
                    Routines.setShortJComponentSizes( label, lbl_pref_sz );
                cmd_path2tracelib_panel.add( label );
                    cmd_path2tracelib = new JTextField();
                    // Routines.setShortJComponentSizes( cmd_path2tracelib,
                    //                                   fld_pref_sz );
                cmd_path2tracelib_panel.add( cmd_path2tracelib );
                Routines.setShortJComponentSizes( cmd_path2tracelib_panel,
                                                  row_pref_sz );

            lower_panel.add( cmd_path2tracelib_panel );
            lower_panel.add( Box.createVerticalStrut( 4 ) );


            cmd_splitter = new JSplitPane( JSplitPane.VERTICAL_SPLIT, true,
                                           upper_panel, lower_panel );
            cmd_splitter.setAlignmentX( Component.CENTER_ALIGNMENT );
            cmd_splitter.setOneTouchExpandable( true );
            err_msg = null;
            try {
                cmd_splitter.setResizeWeight( 1.0d );
            } catch ( NoSuchMethodError err ) {
                err_msg =
                  "Method JSplitPane.setResizeWeight() cannot be found.\n"
                + "This indicates you are running an older Java2 RunTime,\n"
                + "like the one in J2SDK 1.2.2 or older. If this is the case,\n"
                + "some features in Convertor window may not work correctly,\n"
                + "For instance, resize of the window may not resize upper \n"
                + "TextArea.  Manuel movement of splitter is needed.\n";
            }

       super.add( cmd_splitter );
       super.add( Box.createVerticalStrut( 4 ) );

            JPanel  cmd_button_panel = new JPanel();
            cmd_button_panel.setLayout( new BoxLayout( cmd_button_panel,
                                                       BoxLayout.X_AXIS ) );
            cmd_button_panel.setAlignmentX( Component.CENTER_ALIGNMENT );
            cmd_button_panel.add( Box.createHorizontalGlue() );

                btn_insets          = new Insets( 2, 4, 2, 4 );

                cmd_start_btn = new JButton( "Convert" );
                icon_URL = getURL( Const.IMG_PATH + "Convert24.gif" );
                if ( icon_URL != null ) {
                    cmd_start_btn.setIcon( new ImageIcon( icon_URL ) );
                    cmd_start_btn.setVerticalTextPosition(
                                  AbstractButton.CENTER );
                    cmd_start_btn.setHorizontalTextPosition(
                                  AbstractButton.RIGHT );
                    cmd_start_btn.setMargin( btn_insets );
                }
                cmd_start_btn.setToolTipText(
                    "Proceed with the selected logfile conversion." );
            cmd_button_panel.add( cmd_start_btn );
            cmd_button_panel.add( Box.createHorizontalGlue() );

                cmd_stop_btn = new JButton( " Stop " );
                icon_URL = getURL( Const.IMG_PATH + "Stop24.gif" );
                if ( icon_URL != null ) {
                    cmd_stop_btn.setIcon( new ImageIcon( icon_URL ) );
                    cmd_stop_btn.setVerticalTextPosition(
                                 AbstractButton.CENTER );
                    cmd_stop_btn.setHorizontalTextPosition(
                                 AbstractButton.RIGHT );
                    // cmd_stop_btn.setMargin( btn_insets );
                }
                cmd_stop_btn.setToolTipText(
                    "Stop the ongoing logfile conversion." );
            cmd_button_panel.add( cmd_stop_btn );
            cmd_button_panel.add( Box.createHorizontalGlue() );

                cmd_help_btn = new JButton( " Usage " );
                icon_URL = getURL( Const.IMG_PATH + "About24.gif" );
                if ( icon_URL != null ) {
                    cmd_help_btn.setIcon( new ImageIcon( icon_URL ) );
                    cmd_help_btn.setVerticalTextPosition(
                                 AbstractButton.CENTER );
                    cmd_help_btn.setHorizontalTextPosition(
                                 AbstractButton.RIGHT );
                    // cmd_help_btn.setMargin( btn_insets );
                }
                cmd_help_btn.setToolTipText(
                    "Usage information of the selected logfile convertor." );
            cmd_button_panel.add( cmd_help_btn );
            cmd_button_panel.add( Box.createHorizontalGlue() );

                cmd_close4cancel_btn = new JButton( "Cancel" );
                icon_URL = getURL( Const.IMG_PATH + "ConvertCancel24.gif" );
                if ( icon_URL != null ) {
                    cmd_close4cancel_btn.setIcon( new ImageIcon( icon_URL ) );
                    cmd_close4cancel_btn.setVerticalTextPosition(
                                         AbstractButton.CENTER );
                    cmd_close4cancel_btn.setHorizontalTextPosition(
                                         AbstractButton.RIGHT );
                    // cmd_close4cancel_btn.setMargin( btn_insets );
                }
                cmd_close4cancel_btn.setToolTipText( "Close this panel." );
            cmd_button_panel.add( cmd_close4cancel_btn );
            cmd_button_panel.add( Box.createHorizontalGlue() );

            cmd_close4ok_btn = null;
            if ( has_close4ok_btn ) {
                cmd_close4ok_btn = new JButton( "OK" );
                icon_URL = getURL( Const.IMG_PATH + "ConvertOk24.gif" );
                if ( icon_URL != null ) {
                    cmd_close4ok_btn.setIcon( new ImageIcon( icon_URL ) );
                    cmd_close4ok_btn.setVerticalTextPosition(
                                     AbstractButton.CENTER );
                    cmd_close4ok_btn.setHorizontalTextPosition(
                                     AbstractButton.RIGHT );
                    // cmd_close4ok_btn.setMargin( btn_insets );
                }
                cmd_close4ok_btn.setToolTipText( 
                    "Display the last converted SLOG2 logfile "
                  + "and Exit this dialog box." );
                cmd_button_panel.add( cmd_close4ok_btn );
                cmd_button_panel.add( Box.createHorizontalGlue() );
            }

        super.add( cmd_button_panel );
    }

    private void initAllTextFields()
    {
        String           path2jardir;
        String           option4jvm;

        ConvertorConst.initializeSystemProperties();

        // set the path to JVM 
        cmd_path2jvm.setText( ConvertorConst.getDefaultPathToJVM() );

        // set the path to all the jar files
        path2jardir  = ConvertorConst.getDefaultPathToJarDir();
        cmd_path2jardir.setText( path2jardir );

        // set the JVM option
        option4jvm  = null;
        try {
            option4jvm  = cmd_option4jvm.getText();
        } catch ( NullPointerException err ) {}
        if ( option4jvm == null || option4jvm.length() <= 0 );
            cmd_option4jvm.setText( "-Xms32m -Xmx64m" );
    }



    public String selectLogFile()
    {
        int   istat;
        istat = file_chooser.showOpenDialog( top_window );
        if ( istat == LogFileChooser.APPROVE_OPTION ) {
            File   selected_file, selected_dir;
            selected_file = file_chooser.getSelectedFile();
            if ( selected_file != null ) {
                selected_dir  = selected_file.getParentFile();
                if ( selected_dir != null )
                    file_chooser.setCurrentDirectory( selected_dir );
                return selected_file.getPath();
            }
        }
        else
            Dialogs.info( top_window, "No file chosen", null );
        return null;
    }

    private void printSelectedConvertorHelp()
    {
        String              convertor;
        String              path2jardir;
        String              path2tracelib;
        String              jar_path;
        RuntimeExecCommand  exec_cmd;
        File                jar_file;
        Runtime             runtime;
        Process             proc;
        InputStreamThread   proc_err_task, proc_out_task;

        convertor = (String) cmd_pulldown.getSelectedItem();

        //  Set the path to the jar file
        path2jardir = cmd_path2jardir.getText();
        jar_path  = ConvertorConst.getDefaultJarPath( path2jardir, convertor );
        jar_file  = new File( jar_path );
        if ( ! jar_file.exists() ) {
            Dialogs.error( top_window, jar_path + " does not exist!" );
            return;
        }
        if ( ! jar_file.canRead() ) {
            Dialogs.error( top_window, jar_path + " is NOT readable!\n" );
            return;
        }

        exec_cmd = new RuntimeExecCommand();
        exec_cmd.addWholeString( cmd_path2jvm.getText() );
        exec_cmd.addTokenizedString( cmd_option4jvm.getText() );

        path2tracelib = cmd_path2tracelib.getText();
        if ( path2tracelib != null && path2tracelib.length() > 0 )
            exec_cmd.addWholeString( "-Djava.library.path=" + path2tracelib );

        exec_cmd.addWholeString( "-jar" );
        exec_cmd.addWholeString( jar_path );
        exec_cmd.addWholeString( "-h" );

        cmd_textarea.append( "Executing " + exec_cmd.toString() + "...." );
        
        runtime  = Runtime.getRuntime();
        try {
            proc = runtime.exec( exec_cmd.toStringArray() );
            proc_err_task = new InputStreamThread( proc.getErrorStream(),
                                                   "Error", cmd_textarea );
            proc_out_task = new InputStreamThread( proc.getInputStream(),
                                                   "Output", cmd_textarea );
            proc_err_task.start();
            proc_out_task.start();

            // Block THIS thread till process returns!
            int proc_istatus = proc.waitFor();
            // Clean up InputStreamThread's when the proces is done.
            proc_err_task.stopRunning();
            proc_err_task = null;
            proc_out_task.stopRunning();
            proc_out_task = null;

            cmd_textarea.append( "\n> Ending with exit status "
                               + proc_istatus + "\n" );
        } catch ( Throwable err ) {
            err.printStackTrace();
        }
    }


    private SwingProcessWorker convertSelectedLogFile()
    {
        String              convertor;
        String              path2jardir, path2tracelib;
        String              infile_name, outfile_name, jar_path;
        String              option4jar;
        File                infile, outfile, jar_file;
        InputLog            slog_ins;
        RuntimeExecCommand  exec_cmd;

        SwingProcessWorker  conv_worker;
        ProgressAction      conv_progress;

        // Check the validity of the Input File
        infile_name   = cmd_infile.getText();
        infile        = new File( infile_name );
        if ( ! infile.exists() ) {
            Dialogs.error( top_window,
                           infile_name + " does not exist!\n"
                         + "No conversion will take place." );
            return null;
        }
        if ( infile.isDirectory() ) {
            Dialogs.error( top_window,
                           infile_name + " is a directory!\n"
                         + "No conversion will take place." );
            return null;
        }
        if ( ! infile.canRead() ) {
            Dialogs.error( top_window,
                           "File " + infile_name + " is NOT readable!\n"
                         + "No conversion will take place." );
            return null;
        }
        slog_ins = null;
        try {
            slog_ins = new InputLog( infile_name );
        } catch ( NullPointerException nperr ) {
            slog_ins = null;
        } catch ( Exception err ) {
            slog_ins = null;
        }
        if ( slog_ins != null && slog_ins.isSLOG2() ) {
            Dialogs.error( top_window,
                           infile_name + " is already a SLOG-2 file!\n"
                         + "No conversion will take place." );
            cmd_outfile.setText( infile_name );
            return null;
        }

        // Check the validity of the Output File
        outfile_name  = cmd_outfile.getText();
        outfile       = new File( outfile_name );
        if ( outfile.exists() ) {
            if ( outfile.isDirectory() ) {
                Dialogs.error( top_window,
                               outfile_name + " is a directory!\n"
                             + "No conversion will take place." );
                return null;
            }
            if ( ! outfile.canWrite() ) {
                Dialogs.error( top_window,
                               "File " + outfile_name + " cannot be written!\n"
                             + "No conversion will take place." );
                return null;
            }
            if ( ! Dialogs.confirm( top_window,
                                    outfile_name + " already exists! "
                                  + "Do you want to overwrite it ?" ) ) {
                Dialogs.info( top_window,
                              "Please change the output filename "
                            + "and restart the conversion again.",
                              null );
                return null;
            }
            outfile.delete();
        }

        convertor = (String) cmd_pulldown.getSelectedItem();

        //  Set the path to the jar file
        path2jardir = cmd_path2jardir.getText();
        jar_path  = ConvertorConst.getDefaultJarPath( path2jardir, convertor );
        jar_file  = new File( jar_path );
        if ( ! jar_file.exists() ) {
            Dialogs.error( top_window, jar_path + " does not exist!" );
            return null;
        }

        exec_cmd = new RuntimeExecCommand();
        exec_cmd.addWholeString( cmd_path2jvm.getText() );
        exec_cmd.addTokenizedString( cmd_option4jvm.getText() );

        path2tracelib = cmd_path2tracelib.getText();
        if ( path2tracelib != null && path2tracelib.length() > 0 )
            exec_cmd.addWholeString( "-Djava.library.path=" + path2tracelib );

        exec_cmd.addWholeString( "-jar" );
        exec_cmd.addWholeString( jar_path );

        option4jar  = cmd_option4jar.getText();
        if ( option4jar != null && option4jar.length() > 0 )
            exec_cmd.addTokenizedString( option4jar );

        exec_cmd.addWholeString( "-o" );
        exec_cmd.addWholeString( outfile_name );
        exec_cmd.addWholeString( infile_name );

        /*
           Start a SwingWorker thread to execute the process:
           Prepare a progress action for the JProgressBar for the SwingWorker
        */
        conv_progress = new ProgressAction( cmd_outfile_size, cmd_progress );
        conv_progress.initialize( infile, outfile );
        conv_worker = new SwingProcessWorker( this, cmd_textarea );
        conv_worker.initialize( exec_cmd.toStringArray(), conv_progress );
        conv_worker.start();

        return conv_worker;
    }

    private void resetAllButtons( boolean isConvertingLogfile )
    {
        cmd_start_btn.setEnabled( !isConvertingLogfile );
        cmd_stop_btn.setEnabled( isConvertingLogfile );
        cmd_help_btn.setEnabled( !isConvertingLogfile );
        if ( cmd_close4cancel_btn != null )
            cmd_close4cancel_btn.setEnabled( !isConvertingLogfile );
        if ( cmd_close4ok_btn != null ) {
            // The scenarios that logconv_worker == null are either the
            // process has not been started or it has been stopped by user.
            if ( logconv_worker != null && logconv_worker.isEndedNormally() )
                cmd_close4ok_btn.setEnabled( !isConvertingLogfile );
            else
                cmd_close4ok_btn.setEnabled( false );
        }
    }

    // Interface for WaitingContainer (used by SwingProcessWorker)
    public void initializeWaiting()
    {
        Routines.setComponentAndChildrenCursors( cmd_splitter,
                                                 CustomCursor.Wait );
        this.resetAllButtons( true );
    }

    // Interface for WaitingContainer (used by SwingProcessWorker)
    public void finalizeWaiting()
    {
        this.resetAllButtons( false );
        Routines.setComponentAndChildrenCursors( cmd_splitter,
                                                 CustomCursor.Normal );
    }

    public void addActionListenerForOkayButton( ActionListener action )
    {
        if ( action != null && cmd_close4ok_btn != null )
            cmd_close4ok_btn.addActionListener( action );
    }

    public void addActionListenerForCancelButton( ActionListener action )
    {
        if ( action != null && cmd_close4cancel_btn != null )
            cmd_close4cancel_btn.addActionListener( action );
    }

    public String getOutputSLOG2Name()
    {
        return cmd_outfile.getText();
    }



    private class LogNameListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String infile_name, outfile_name;
            infile_name   = cmd_infile.getText();
            outfile_name  = ConvertorConst.getDefaultSLOG2Name( infile_name );
            cmd_outfile.setText( outfile_name );
        }
    }

    private class InputFileSelectorListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String filename = selectLogFile();
            if ( filename != null && filename.length() > 0 ) {
                cmd_infile.setText( filename );
                printSelectedConvertorHelp();
            }
        }
    }

    private class OutputFileSelectorListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String filename = selectLogFile();
            if ( filename != null && filename.length() > 0 ) {
                cmd_outfile.setText( filename );
            }
        }
    }

    private class PulldownListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String  convertor, path2jardir;
            convertor    = (String) cmd_pulldown.getSelectedItem();
            path2jardir  = ConvertorConst.getDefaultPathToJarDir();
            cmd_path2tracelib.setText(
                ConvertorConst.getDefaultTraceLibPath( convertor,
                                                       path2jardir ) );
            printSelectedConvertorHelp();
        }
    }

    private class JarDirectoryListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            String  convertor, path2jardir;
            String  def_path2tracelib, cur_path2tracelib, new_path2tracelib;

            convertor    = (String) cmd_pulldown.getSelectedItem();

            cur_path2tracelib = cmd_path2tracelib.getText();
            if ( cur_path2tracelib == null )
                cur_path2tracelib = "";

            // Check if the TraceLibrary Path has been updated,
            // i.e. already synchronized with JAR Directory.
            path2jardir  = cmd_path2jardir.getText();
            new_path2tracelib = ConvertorConst.getDefaultTraceLibPath(
                                               convertor, path2jardir );
            if ( new_path2tracelib == null )
                new_path2tracelib = "";
            if ( cur_path2tracelib.equals( new_path2tracelib ) )
                return;

            // Check if path2tracelib is differnt from the default
            path2jardir  = ConvertorConst.getDefaultPathToJarDir();
            def_path2tracelib = ConvertorConst.getDefaultTraceLibPath(
                                               convertor, path2jardir );
            if ( def_path2tracelib == null )
                def_path2tracelib = "";
            if ( ! cur_path2tracelib.equals( def_path2tracelib ) ) {
                if ( ! Dialogs.confirm( top_window,
                                        "TraceLibrary Path has been modified "
                                      + "from the original default value.\n"
                                      + "Should it be updated by the new "
                                      + "default value based on your modified "
                                      + "JAR Directory ?" ) ) {
                    return;
                }
            }

            // Update the default TraceLibPaths with the modified cmd_path2jar.
            cmd_path2tracelib.setText( new_path2tracelib );
        }
    }

    private class StartConvertorListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            logconv_worker = convertSelectedLogFile();
        }
    }

    private class StopConvertorListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            // Set logconv_worker = null when the conversion is stopped manually
            // so resetAllButtons() can set close4ok button accordingly.
            if ( logconv_worker != null ) {
                logconv_worker.finished();
                logconv_worker  = null;
            }
        }
    }

    private class HelpConvertorListener implements ActionListener
    {
        public void actionPerformed( ActionEvent evt )
        {
            cmd_path2jardir.fireActionPerformed(); // call JarDirectoryListener
            printSelectedConvertorHelp();
        }
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.first;

import java.awt.Window;
import javax.swing.JTextField;
import java.util.List;
import java.io.File;
import java.io.FileNotFoundException;

import logformat.slog2.input.InputLog;
import viewer.common.SwingWorker;
import viewer.common.Dialogs;
import viewer.common.TopWindow;
import viewer.common.Parameters;
import viewer.common.PreferenceFrame;
import viewer.common.LogFileChooser;
import viewer.convertor.ConvertorDialog;
import viewer.legends.LegendFrame;
import viewer.timelines.TimelineFrame;

public class LogFileOperations
{
    private        LogFileChooser    file_chooser;

    private        InputLog          slog_ins;
    private        PreferenceFrame   pptys_frame;
    private        LegendFrame       legend_frame;
    private        TimelineFrame     timeline_frame;

    public LogFileOperations( boolean isApplet )
    {
        file_chooser    = new LogFileChooser( isApplet );

        slog_ins        = null;
        legend_frame    = null;
        timeline_frame  = null;
    }

    public void init()
    {
        /*  Initialization  */
        Parameters.initSetupFile();
        Parameters.readFromSetupFile( TopWindow.First.getWindow() );
        Parameters.initStaticClasses();
        pptys_frame     = new PreferenceFrame();
        pptys_frame.setVisible( false );
    }

    private static InputLog createInputLog( Window window, String pathname )
    {
        String logname = pathname.trim();
        if ( logname != null && logname.length() > 0 ) {
            File logfile = new File( logname );
            if ( ! logfile.exists() ) {
                Dialogs.error( window,
                               "File Not Found when initializing "
                             + logname + "." );
                return null;
            }
            if ( logfile.isDirectory() ) {
                Dialogs.error( window,
                               logname + " is a directory." );
                return null;
            }
            if ( ! logfile.canRead() ) {
                Dialogs.error( window,
                               "File " + logname + " cannot be read." );
                return null;
            }

            InputLog slog = null;
            try {
                slog = new InputLog( logname );
            } catch ( NullPointerException nperr ) {
                Dialogs.error( window,
                               "NullPointerException when initializing "
                             + logname + "!" );
                return null;
            } catch ( Exception err ) {
                Dialogs.error( window,
                               "EOFException when initializing "
                             + logname + "!" );
                return null;
            }
            return slog;
        }
        else {
            if ( logname == null )
                Dialogs.error( window, "Null pathname!" );
            else // if ( logname.length() == 0 )
                Dialogs.error( window, "pathname is empty!" );
            return null;
        }

    }

    /* This disposes all the windows and InputLog related resources. */
    public void disposeLogFileAndResources()
    {
        if ( slog_ins != null ) {
            TopWindow.Legend.disposeAll();
            slog_ins.close();
            slog_ins        = null;
            legend_frame    = null;
            timeline_frame  = null;
        }
    }

    public String selectLogFile()
    {
        int   istat;
        istat = file_chooser.showOpenDialog( TopWindow.First.getWindow() );
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
            Dialogs.info( TopWindow.First.getWindow(), "No file chosen", null );
        return null;
    }

    public String convertLogFile( String filename )
    {
        return ConvertorDialog.convertLogFile( TopWindow.First.getWindow(),
                                               file_chooser, filename );
    }

    /*
        this.disposeLogFileAndResources() has to be called
        before this.openLogFile() can be invoked.
    */
    public List openLogFile( JTextField  logname_txtfld )
    {
        String filename, new_filename, err_msg;
        filename  = logname_txtfld.getText();
        slog_ins  = LogFileOperations.createInputLog(
                           TopWindow.First.getWindow(), filename );
        if ( slog_ins == null ) {
            Dialogs.error( TopWindow.First.getWindow(), "Null InputLog!" );
            return null;
        }
        if ( ! slog_ins.isSLOG2() ) {
            slog_ins = null;
            if ( Dialogs.confirm( TopWindow.First.getWindow(),
                                  filename + " is NOT a SLOG-2 file!\n"
                                + "Do you want to convert it to SLOG-2 format "
                                + "readable by this viewer?" ) ) {
                new_filename = ConvertorDialog.convertLogFile(
                                               TopWindow.First.getWindow(),
                                               file_chooser, filename );
                // Since ConvertorDialog can return null if ConvertorPanel's
                // Cancel button is clicked, needs to check for null return.
                // But don't check for new_filename.length()==0, so subsequent
                // createInputLog() can emit a warning message for zero-length.
                if ( new_filename != null ) {
                    logname_txtfld.setText( new_filename );
                    return openLogFile( logname_txtfld );
                }
                else
                    return null;
            }
            else
                return null;
        }
        err_msg   = null;
        if ( (err_msg = slog_ins.getCompatibleHeader() ) != null ) {
            if ( ! Dialogs.confirm( TopWindow.First.getWindow(),
                             err_msg
                          + "Check the following version history "
                          + "for compatibility.\n\n"
                          + logformat.slog2.Const.VERSION_HISTORY + "\n"
                          + "Do you still want to continue reading "
                          + "the logfile ?" ) ) {
                slog_ins = null;
                return null;
            }
        }
        slog_ins.initialize();
        legend_frame = new LegendFrame( slog_ins );
        legend_frame.pack();
        TopWindow.layoutIdealLocations();
        legend_frame.setVisible( true );
        return (List) slog_ins.getLineIDMapList();
    }

    /*
    public void createTimelineWindow( int view_ID )
    {
        if ( slog_ins != null && view_ID >= 0 ) {
            timeline_frame  = new TimelineFrame( slog_ins, view_ID );
            timeline_frame.pack();
            TopWindow.layoutIdealLocations();
            timeline_frame.setVisible( true );
            timeline_frame.init();
        }
    }
    */

    /*  The following works as if no thread is used!
    public void createTimelineWindow( final int view_ID )
    {
        if ( slog_ins != null && view_ID >= 0 ) {
            Runnable create_timeline_thread = new Runnable() {
                public void run()
                {
                    timeline_frame  = new TimelineFrame( slog_ins, view_ID );
                    timeline_frame.pack();
                    TopWindow.layoutIdealLocations();
                    timeline_frame.setVisible( true );
                    timeline_frame.init();
                }
            };
            SwingUtilities.invokeLater( create_timeline_thread );
        }
    }
    */

    public void createTimelineWindow( final int view_ID )
    {
        if ( slog_ins != null && view_ID >= 0 ) {
            SwingWorker create_timeline_worker = new SwingWorker() {
                public Object construct()
                {
                    timeline_frame = new TimelineFrame( slog_ins, view_ID );
                    return null;  // returned value is not needed
                }
                public void finished()
                {
                    timeline_frame.pack();
                    TopWindow.layoutIdealLocations();
                    timeline_frame.setVisible( true );
                    timeline_frame.init();
                }
            };
            create_timeline_worker.start();
        }
    }

    public void showLegendWindow()
    {
        if ( slog_ins != null && legend_frame != null ) {
            legend_frame.pack();
            TopWindow.layoutIdealLocations();
            legend_frame.setVisible( true );
        }
    }

    public void showPreferenceWindow()
    {
        if ( pptys_frame != null ) {
            pptys_frame.pack();
            TopWindow.layoutIdealLocations();
            pptys_frame.setVisible( true );
            pptys_frame.toFront();
        }
    }
}

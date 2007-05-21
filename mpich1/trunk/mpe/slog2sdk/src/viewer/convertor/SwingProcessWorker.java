/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.awt.Component;
import java.awt.event.ActionListener;
import javax.swing.Timer;

import viewer.common.SwingWorker;

public class SwingProcessWorker extends SwingWorker
{
    private WaitingContainer   container;
    private AdvancingTextArea  textarea;

    private String[]           exec_cmd_ary;
    private Timer              process_timer;
    private ActionListener     process_progress;
    private Process            process;
    private InputStreamThread  process_err_task, process_out_task;
    private int                process_istatus;

    public SwingProcessWorker( WaitingContainer     proc_container,
                               AdvancingTextArea    proc_textarea )
    {
        container         = proc_container;
        textarea          = proc_textarea;

        process_progress  = null;
        process_timer     = new Timer( 500, process_progress );
        process_timer.setInitialDelay( 0 );
        process_timer.setCoalesce( true );

        process           = null;
        process_err_task  = null;
        process_out_task  = null;
    }

    public void initialize( String[]        command_strs,
                            ActionListener  progress_action )
    {
        StringBuffer  cmd_strbuf; 

        exec_cmd_ary          = command_strs;

        if ( process_progress != null )
            process_timer.removeActionListener( process_progress );
        process_progress  = progress_action;
        process_timer.addActionListener( process_progress );

        cmd_strbuf = new StringBuffer( "Executing " );
        for ( int idx = 0; idx < exec_cmd_ary.length; idx++ )
            cmd_strbuf.append( exec_cmd_ary[ idx ] + " " );
        cmd_strbuf.append( "...."  );
        textarea.append( cmd_strbuf.toString() );
        Runtime  runtime = Runtime.getRuntime();
        try {
            process = runtime.exec( exec_cmd_ary );
            process_err_task = new InputStreamThread( process.getErrorStream(),
                                                      "Error", textarea );
            process_out_task = new InputStreamThread( process.getInputStream(),
                                                      "Output", textarea );
            process_istatus  = Integer.MIN_VALUE;
            container.initializeWaiting();
        } catch ( Throwable err ) {
            textarea.append( "\n> Ending with unexpected Exception! "
                           + "Details in stderr." );
            err.printStackTrace();
        }
    }

    /*
       SwingWorker.construct() executed in non-event-dispatching thread,
       i.e. separate thread from GUI executing thread
       Process.waitFor() blocks the invoking thread till the process returns.
    */
    public Object construct()
    {
        try {
            if ( process_err_task != null )
                process_err_task.start();
            if ( process_out_task != null )
                process_out_task.start();
            process_timer.start();
            // Block until the process terminates!
            process_istatus = process.waitFor();
        } catch ( Throwable err ) {
            err.printStackTrace();
        }
        return null;
    }   // End of construct()

    /*
       SwingWorker.finished() executed in event-dispatching thread,
       i.e. GUI executing thread.
    */
    public void finished()
    {
        // Clean up InputStreamThread's when the process is done.
        process_timer.stop();
        if ( process_err_task != null ) {
            process_err_task.stopRunning();
            process_err_task  = null;
        }
        if ( process_out_task != null ) {
            process_out_task.stopRunning();
            process_out_task  = null;
        }
        if ( process != null ) {
            process.destroy();
            process  = null;
        }
        // progress_action.finish();
        textarea.append( "\n> Ending with exit status "
                       + process_istatus + "\n" );
        container.finalizeWaiting();
    }   // End of finished()

    //  Check if the process is terminated normally, process_istatus == 0.
    public boolean isEndedNormally()
    {
        return process_istatus == 0;
    }
}

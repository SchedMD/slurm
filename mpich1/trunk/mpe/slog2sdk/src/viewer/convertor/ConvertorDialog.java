/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.WindowEvent;
import java.awt.event.WindowAdapter;
import javax.swing.JFrame;
import javax.swing.JDialog;
import javax.swing.WindowConstants;

import viewer.common.LogFileChooser;

public class ConvertorDialog extends JDialog
{
    private static String          in_filename;      // For main()

    private        ConvertorPanel  top_panel;

    public ConvertorDialog( JFrame          ancestor_frame,
                            LogFileChooser  file_chooser )
    {
        // Make this a Modal Dialog
        super( ancestor_frame, "Logfile Convertor", true );
        super.setDefaultCloseOperation( WindowConstants.DO_NOTHING_ON_CLOSE );

        top_panel = new ConvertorPanel( file_chooser );
        super.setContentPane( top_panel );

        super.addWindowListener( new WindowAdapter() {
            public void windowClosing( WindowEvent evt ) {
                ConvertorDialog.this.setVisible( false );
                ConvertorDialog.this.dispose();
            }
        } );

        /* setVisible( true ) */;
    }

    public void init( String trace_filename )
    { top_panel.init( trace_filename ); }

    /*
    public void addActionListenerForOkayButton( ActionListener action )
    { top_panel.addActionListenerForOkayButton( action ); }

    public void addActionListenerForCancelButton( ActionListener action )
    { top_panel.addActionListenerForCancelButton( action ); }
    */

    public static String convertLogFile( JFrame          frame,
                                         LogFileChooser  chooser,
                                         String          filename )
    {
        ConvertorDialog        conv_dialog;
        CloseAction            win_closer;
        CloseToRetrieveAction  logname_fetcher;

        conv_dialog     = new ConvertorDialog( frame, chooser );

        logname_fetcher = new CloseToRetrieveAction( conv_dialog );
        conv_dialog.top_panel.addActionListenerForOkayButton( logname_fetcher );

        win_closer      = new CloseAction( conv_dialog );
        conv_dialog.top_panel.addActionListenerForCancelButton( win_closer );

        conv_dialog.pack();
        conv_dialog.init( filename );
        conv_dialog.setVisible( true );
        // As ConvertorDialog is modal, it will block until it is closed
        // and the program logic stays here until user closes the dialog.
        return logname_fetcher.getFilename();
    }

    private static class CloseAction implements ActionListener
    {
        private ConvertorDialog  convertor;

        public CloseAction( ConvertorDialog convertor_dialog )
        { convertor  = convertor_dialog; }

        public void actionPerformed( ActionEvent evt )
        {
            convertor.setVisible( false );
            convertor.dispose();
        }
    }

    private static class CloseToRetrieveAction implements ActionListener
    {
        private ConvertorDialog  convertor;
        private String           filename;

        public CloseToRetrieveAction( ConvertorDialog convertor_dialog )
        {
            convertor  = convertor_dialog;
            filename   = null;
        }

        public String  getFilename()
        { return filename; }

        public void actionPerformed( ActionEvent evt )
        {
            filename = convertor.top_panel.getOutputSLOG2Name();
            convertor.setVisible( false );
            convertor.dispose();
        }
    }
}

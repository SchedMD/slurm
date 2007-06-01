/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */


package viewer.common;

import javax.swing.JFileChooser;
import javax.swing.filechooser.FileFilter;
import java.io.File;

public class LogFileChooser extends JFileChooser
{
    private boolean  isApplet;

    public LogFileChooser( boolean isTopApplet )
    {
        super( System.getProperty( "user.dir" ) );
        super.setDialogTitle( "Select SLOG-2 file" );

        isApplet = isTopApplet;

        FileFilter  filter;
        if ( isApplet ) {
            super.setAcceptAllFileFilterUsed( false );
            filter = new LogRefuseDirFilter( new String[]{ "slog2" } );
            super.setFileFilter( filter );
        }
        else {
            filter = new LogPermitDirFilter( new String[]{ "slog2" } );
            super.addChoosableFileFilter( filter );
            filter = new LogPermitDirFilter( new String[]{ "clog2" } );
            super.addChoosableFileFilter( filter );
            filter = new LogPermitDirFilter( new String[]{ "clog" } );
            super.addChoosableFileFilter( filter );
            filter = new LogPermitDirFilter( new String[]{ "rlog" } );
            super.addChoosableFileFilter( filter );
            filter = new LogPermitDirFilter(
                         new String[]{ "slog2", "clog2", "clog", "rlog" } );
            super.addChoosableFileFilter( filter );
        }
    }

    public boolean isTraversable( File file )
    {
        if ( isApplet )
            if ( file != null )
                return ! file.isDirectory();
            else
                return false;
        else
            return super.isTraversable( file );
    }
}

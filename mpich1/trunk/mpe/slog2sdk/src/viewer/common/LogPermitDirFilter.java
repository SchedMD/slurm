/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import javax.swing.filechooser.FileFilter;
import java.io.File;

public class LogPermitDirFilter extends FileFilter
{
    String        extns[];
    StringBuffer  description;

    public LogPermitDirFilter( String[] in_extns )
    {
        extns        = new String[ in_extns.length ];
        description  = new StringBuffer();
        for ( int idx = 0; idx < extns.length; idx++ ) {
            extns[ idx ] = ( new String(in_extns[ idx ]) ).trim();
            description.append( " *." + extns[ idx ] );
        }
        description.append( " and directories" );
    }

    // Accept __Directories__ and Accept files with one of extns[] suffix.
    public boolean accept( File file )
    {
        if ( file.isDirectory() )
            return true;

        String extension = LogPermitDirFilter.getFileExtension( file );
	if ( extension != null ) {
            for ( int idx = 0; idx < extns.length; idx++ ) {
                if ( extension.equals( extns[ idx ] ) )
                    return true;
            }
    	}

        return false;
    }
    
    // The description of this filter
    public String getDescription()
    {
        return description.toString();
    }

    private static String getFileExtension( File file )
    {
        String name  = file.getName();
        int    idx   = name.lastIndexOf( '.' );

        String ext   = null;
        if ( idx > 0 && idx < name.length() - 1 )
            ext = name.substring( idx+1 ).toLowerCase();
        return ext;
    }
}


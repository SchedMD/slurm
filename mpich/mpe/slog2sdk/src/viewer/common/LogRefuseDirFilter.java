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

public class LogRefuseDirFilter extends FileFilter
{
    String        extns[];
    StringBuffer  description;

    public LogRefuseDirFilter( String[] in_extns )
    {
        extns        = new String[ in_extns.length ];
        description  = new StringBuffer();
        for ( int idx = 0; idx < extns.length; idx++ ) {
            extns[ idx ] = ( new String(in_extns[ idx ]) ).trim();
            description.append( " *." + extns[ idx ] );
        }
    }

    // Refuse __Directories__ but Accept files with one of extns[] suffix.
    public boolean accept( File file )
    {
        if ( file.isDirectory() )
            return false;

        String extension = LogRefuseDirFilter.getFileExtension( file );
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


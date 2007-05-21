/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

import java.util.StringTokenizer;

public class TraceName
{
    public static final String CLOG2_EXT   = ".clog2";
    public static final String CLOG_EXT    = ".clog";
    public static final String RLOG_EXT    = ".rlog";
    public static final String UTE_EXT     = ".ute";
    public static final String TXT_EXT     = ".txt";
    public static final String SLOG1_EXT   = ".slog";
    public static final String SLOG2_EXT   = ".slog2";

    public static String getLogFormatExtension( String tracename )
    {
        if ( tracename.endsWith( CLOG2_EXT ) )
            return CLOG2_EXT;
        else if ( tracename.endsWith( CLOG_EXT ) )
            return CLOG_EXT;
        else if ( tracename.endsWith( RLOG_EXT ) )
            return RLOG_EXT ;
        else if ( tracename.endsWith( UTE_EXT ) )
            return UTE_EXT;
        else if ( tracename.endsWith( SLOG1_EXT ) )
            return SLOG1_EXT ;
        else if ( tracename.endsWith( SLOG2_EXT ) )
            return SLOG2_EXT ;
        else if ( tracename.endsWith( TXT_EXT ) )
            return TXT_EXT;
        else
            return "";
    }

    //private static final String DELIMITERS = "[]{}()~!@#$%^&*\\;`/? \t\n\r\f";

    // Get rid of all possible file separator characters
    private static final String DELIMITERS = "[]{}()~!@#$%^&*;`? \t\n\r\f";

    private static String stripInvalidChar( String tracename )
    {
        String  delims;
        if ( System.getProperty( "file.separator" ).equals( "/" ) )
            delims  = DELIMITERS + ":\\";   // Unix
        else
            delims  = DELIMITERS + "/";     // Windows
        StringTokenizer  tokens = new StringTokenizer( tracename, delims );
        StringBuffer     strbuf = new StringBuffer();
        if ( tokens.hasMoreTokens() ) {
            strbuf.append( tokens.nextToken() );
            while ( tokens.hasMoreTokens() ) {
                strbuf.append( '_' );
                strbuf.append( tokens.nextToken() );
            }
        }
        if ( strbuf.charAt( 0 ) == '-' )
            return "TRACE_" + strbuf.toString();
        else
            return strbuf.toString();
    }

    public static String getDefaultSLOG2Name( String tracename )
    {
        if ( tracename.endsWith( CLOG2_EXT ) )
            return tracename.substring( 0, tracename.lastIndexOf( CLOG2_EXT ) )
                 + SLOG2_EXT ;
        else if ( tracename.endsWith( CLOG_EXT ) )
            return tracename.substring( 0, tracename.lastIndexOf( CLOG_EXT ) )
                 + SLOG2_EXT ;
        else if ( tracename.endsWith( RLOG_EXT ) )
            return tracename.substring( 0, tracename.lastIndexOf( RLOG_EXT ) )
                 + SLOG2_EXT ;
        else if ( tracename.endsWith( TXT_EXT ) )
            return tracename.substring( 0, tracename.lastIndexOf( TXT_EXT ) )
                 + SLOG2_EXT ;
        else {
            String prefix_name = stripInvalidChar( tracename );
            if ( prefix_name.length() > 0 )
                if ( prefix_name.endsWith( "." ) )
                    return prefix_name + SLOG2_EXT.substring( 1 );
                else
                    return prefix_name + SLOG2_EXT;
            else
                return "trace" + SLOG2_EXT;
        }
    }
}

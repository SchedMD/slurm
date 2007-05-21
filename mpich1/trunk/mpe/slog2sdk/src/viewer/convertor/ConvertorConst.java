/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.convertor;

import java.util.Properties;
import java.util.StringTokenizer;
import java.io.File;

import logformat.slog2.TraceName;

public class ConvertorConst
{
    public  static final String  CLOG2_TO_SLOG2     = "  CLOG-2  -->  SLOG-2  ";
    public  static final String  CLOG_TO_SLOG2      = "  CLOG    -->  SLOG-2  ";
    public  static final String  RLOG_TO_SLOG2      = "  RLOG    -->  SLOG-2  ";
    public  static final String  UTE_TO_SLOG2       = "  UTE     -->  SLOG-2  ";

    private static final String  CLOG2_TO_SLOG2_JAR = "clog2TOslog2.jar";
    private static final String  CLOG_TO_SLOG2_JAR  = "clogTOslog2.jar";
    private static final String  RLOG_TO_SLOG2_JAR  = "traceTOslog2.jar";
    private static final String  UTE_TO_SLOG2_JAR   = "traceTOslog2.jar";

    // Assume Unix convention.
    private static       String  FileSeparator      = "/";
    private static       String  PathSeparator      = ":";
    private static       String  JavaHome           = null;
    private static       String  ClassPath          = null;
    private static final String  JVM                = "java";

    // if XXX_TraceLibPath = null, means no need to use -Djava.library.path=
    // if XXX_TraceLibPath contains ".", i.e. Jar Directory.
    private static       String  CLOG2_TraceLibPath = "";
    private static       String  CLOG_TraceLibPath  = "";
    private static       String  RLOG_TraceLibPath  = ".:../trace_rlog/lib";
    private static       String  UTE_TraceLibPath   = ".:/usr/lpp/ppe.perf/lib";



    public  static String getDefaultConvertor( String filename )
    {
        String log_ext = TraceName.getLogFormatExtension( filename );
        if ( log_ext.equals( TraceName.CLOG2_EXT ) )
            return CLOG2_TO_SLOG2;
        else if ( log_ext.equals( TraceName.CLOG_EXT ) )
            return CLOG_TO_SLOG2;
        else if ( log_ext.equals( TraceName.RLOG_EXT ) )
            return RLOG_TO_SLOG2;
        else if ( log_ext.equals( TraceName.UTE_EXT ) )
            return UTE_TO_SLOG2;
        else
            return "";
    }

    public  static String getDefaultSLOG2Name( String filename )
    {
        return TraceName.getDefaultSLOG2Name( filename );
    }

    public  static String getDefaultJarName( String convertor )
    {
        if ( convertor.equals( CLOG2_TO_SLOG2 ) )
            return CLOG2_TO_SLOG2_JAR;
        else if ( convertor.equals( CLOG_TO_SLOG2 ) )
            return CLOG_TO_SLOG2_JAR;
        else if ( convertor.equals( RLOG_TO_SLOG2 ) )
            return RLOG_TO_SLOG2_JAR;
        else if ( convertor.equals( UTE_TO_SLOG2 ) )
            return UTE_TO_SLOG2_JAR;
        else
            return "";
    }

    public  static String getDefaultJarPath( String prefix, String convertor )
    {
        if ( prefix != null && prefix.length() > 0 )
            return prefix + FileSeparator + getDefaultJarName( convertor );
        else
            return getDefaultJarName( convertor );
    }

    public  static String getDefaultTraceLibPath( String  convertor,
                                                  String  prefix )
    {
        if ( convertor.equals( CLOG2_TO_SLOG2 ) )
            return updateLibraryPath( prefix, CLOG2_TraceLibPath );
        else if ( convertor.equals( CLOG_TO_SLOG2 ) )
            return updateLibraryPath( prefix, CLOG_TraceLibPath );
        else if ( convertor.equals( RLOG_TO_SLOG2 ) )
            return updateLibraryPath( prefix, RLOG_TraceLibPath );
        else if ( convertor.equals( UTE_TO_SLOG2 ) )
            return updateLibraryPath( prefix, UTE_TraceLibPath );
        else
            return ".";
    }

    private static boolean replaceCharOfTraceLibPaths( char old_char,
                                                       char new_char )
    {
        if ( old_char != new_char ) {
            if ( CLOG2_TraceLibPath != null )
                CLOG2_TraceLibPath = CLOG2_TraceLibPath.replace( old_char,
                                                                 new_char );
            if ( CLOG_TraceLibPath != null )
                CLOG_TraceLibPath = CLOG_TraceLibPath.replace( old_char,
                                                               new_char );
            if ( RLOG_TraceLibPath != null )
                RLOG_TraceLibPath = RLOG_TraceLibPath.replace( old_char,
                                                               new_char );
            if ( UTE_TraceLibPath != null )
                UTE_TraceLibPath  = UTE_TraceLibPath.replace( old_char,
                                                              new_char );
            return true;
        }
        else
            return false;
    }

    public  static void initializeSystemProperties()
    {
        Properties       sys_pptys;
        String           ppty_str;

        sys_pptys  = System.getProperties();

        ppty_str   = sys_pptys.getProperty( "file.separator" );
        if ( replaceCharOfTraceLibPaths( FileSeparator.charAt( 0 ),
                                         ppty_str.charAt( 0 ) ) )
            FileSeparator = ppty_str;

        ppty_str   = sys_pptys.getProperty( "path.separator" );
        if ( replaceCharOfTraceLibPaths( PathSeparator.charAt( 0 ),
                                         ppty_str.charAt( 0 ) ) )
            PathSeparator = ppty_str;

        JavaHome   = sys_pptys.getProperty( "java.home" );
        ClassPath  = sys_pptys.getProperty( "java.class.path" );
    }

    public  static String getDefaultPathToJVM()
    {
        String  path2jvm;
        File    jvm_file;

        path2jvm = JavaHome + FileSeparator + "bin" + FileSeparator + JVM;
        jvm_file = new File( path2jvm );
        if ( ! FileSeparator.equals( "/" ) ) {
            //  Assume MS Windows, executable needs ".exe" suffix.
            if ( ! path2jvm.endsWith( ".exe" ) ) {
                path2jvm = path2jvm + ".exe";
                jvm_file = new File( path2jvm );
            }
        }
        if ( ! jvm_file.exists() ) {
            path2jvm = JVM;
        }
        return path2jvm;
    }

    public  static String getDefaultPathToJarDir()
    {
        StringTokenizer  paths;
        String           path;
        String           path2jardir;
        int              char_idx;

        // System.out.println( "ClassPath = " + ClassPath );
        path2jardir  = null;
        paths        = new StringTokenizer( ClassPath, PathSeparator );
        while ( paths.hasMoreTokens() && path2jardir == null ) {
            path      = paths.nextToken();
            char_idx  = path.lastIndexOf( FileSeparator );
            if ( char_idx >= 0 )
                path2jardir = path.substring( 0, char_idx );
        }
        return path2jardir;
    }

    /*
    private static boolean isAbsolutePathname( String name )
    {
        if ( name.startsWith( FileSeparator ) )
            return true;

        File[] filesystems = File.listRoots();
        for ( int idx = 0; idx < filesystems.length; idx++ )
             if ( name.startsWith( filesystems[ idx ].getPath() ) )
                 return true;

        return false;
    }
    */

    private static String updateLibraryPath( String  prefix_path,
                                             String  old_libpath )
    {
        StringBuffer     new_libpath;
        StringTokenizer  paths;
        String           path;

        new_libpath  = new StringBuffer();
        paths        = new StringTokenizer( old_libpath, PathSeparator );
        while ( paths.hasMoreTokens() ) {
            path     = paths.nextToken();
            if ( ( new File( path ) ).isAbsolute() )
                // Assume it is full path
                new_libpath.append( path );
            else if ( path.equals( "." ) )
                // Assume . as prefix_path
                new_libpath.append( prefix_path );
            else
                // Assume it is relative path, prepend with prefix_path
                new_libpath.append( prefix_path + FileSeparator + path );
            if ( paths.hasMoreTokens() )
                new_libpath.append( PathSeparator );
        }
        return new_libpath.toString();
    }
}

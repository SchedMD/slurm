/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.zoomable;

public class Profile
{
    private static Diagnosis msg = new Diagnosis();

    public static void initTextArea()
    {
        msg.initTextArea( "Profiling Output" );
    }

    public static void setActive( boolean is_active )
    {
        msg.setActive( is_active );
    }

    public static boolean isActive()
    {
        return msg.isActive();
    }

    public static void setFilename( String in_name )
    throws java.io.IOException
    {
        msg.setFilename( in_name );
    }

    public static void print( String str )
    {
        msg.print( str );
    }

    public static void println( String str )
    {
        msg.println( str );
    }
}

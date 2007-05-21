/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.common;

import java.util.ArrayList;
import java.util.StringTokenizer;

//  A class facilitates the construction of String[] to Runtime.exec( String[] )
public class RuntimeExecCommand extends ArrayList
{
    private String[]  cmd_ary;

    public RuntimeExecCommand()
    {
        super();
        cmd_ary = null;
    }

    public void addWholeString( String cmd_arg )
    {
        if ( cmd_arg != null && cmd_arg.length() > 0 )
            super.add( cmd_arg );
        // make sure that the output of toStringArray() and toString()
        // will be consistent with the content of ArrayList
        cmd_ary = null;
    }

    public void addTokenizedString( String cmd_arg )
    {
        StringTokenizer  cmd_tokens;

        if ( cmd_arg != null && cmd_arg.length() > 0 ) {
            cmd_tokens = new StringTokenizer( cmd_arg );
            while( cmd_tokens.hasMoreTokens() )
                super.add( cmd_tokens.nextToken() );
        }
        // make sure that the output of toStringArray() and toString()
        // will be consistent with the content of ArrayList
        cmd_ary = null;
    }

    public String[] toStringArray()
    {
        if ( cmd_ary == null )
            cmd_ary = (String[]) super.toArray( new String[0] );
        return cmd_ary;
    }

    public String toString()
    {
        StringBuffer  cmd_strbuf;
        if ( cmd_ary == null )
            cmd_ary = (String[]) super.toArray( new String[0] );
        cmd_strbuf = new StringBuffer();
        for ( int idx = 0; idx < cmd_ary.length; idx++ )
            cmd_strbuf.append( cmd_ary[ idx ] + " " );
        return cmd_strbuf.toString();
    }
}

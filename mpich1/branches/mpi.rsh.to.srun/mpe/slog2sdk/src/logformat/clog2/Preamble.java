/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.util.StringTokenizer;
import java.util.NoSuchElementException;
import java.io.DataInputStream;
import java.io.IOException;

// Class corresponds to CLOG_Premable_t
public class Preamble
{
    // BYTESIZE corresponds to CLOG_PREAMBLE_SIZE
    private final static int      BYTESIZE     = 1024;
    // VERSIONSIZE corresponds to CLOG_VERSION_STRLEN;
    private final static int      VERSIONSIZE  = 12;

    private              String   version;
    //  this correspond to CLOG_Premable_t.is_big_endian
    private              String   is_big_endian_title;
    private              boolean  is_big_endian;
    //  this correspond to CLOG_Premable_t.block_size
    private              String   block_size_title;
    private              int      block_size;
    //  this correspond to CLOG_Premable_t.num_buffered_blocks
    private              String   num_blocks_title;
    private              int      num_blocks;
    //  this correspond to CLOG_Premable_t.comm_world_size
    private              String   world_size_title;
    private              int      world_size;
    //  this correspond to CLOG_Premable_t.known_eventID_start
    private              String   known_eventID_start_title;
    private              int      known_eventID_start;
    //  this correspond to CLOG_Premable_t.user_eventID_start
    private              String   user_eventID_start_title;
    private              int      user_eventID_start;
    //  this correspond to CLOG_Premable_t.user_solo_eventID_start
    private              String   user_solo_eventID_start_title;
    private              int      user_solo_eventID_start;
    //  this correspond to CLOG_Premable_t.known_stateID_count
    private              String   known_stateID_count_title;
    private              int      known_stateID_count;
    //  this correspond to CLOG_Premable_t.user_stateID_count
    private              String   user_stateID_count_title;
    private              int      user_stateID_count;
    //  this correspond to CLOG_Premable_t.user_solo_eventID_count
    private              String   user_solo_eventID_count_title;
    private              int      user_solo_eventID_count;

    public boolean readFromDataStream( DataInputStream in )
    {
        byte[]           buffer;
        StringTokenizer  tokens;
        String           str;

        buffer = new byte[ BYTESIZE ];
        try {
            in.readFully( buffer );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return false;
        }

        tokens  = new StringTokenizer( new String( buffer ), "\0" );
        try {
            version = tokens.nextToken().trim();
            is_big_endian_title = tokens.nextToken().trim();
            str                 = tokens.nextToken().trim();
            is_big_endian       =  str.equalsIgnoreCase( "true" )
                                || str.equalsIgnoreCase( "yes" );
            block_size_title    = tokens.nextToken().trim();
            block_size          = Integer.parseInt( tokens.nextToken().trim() );
            num_blocks_title    = tokens.nextToken().trim();
            num_blocks          = Integer.parseInt( tokens.nextToken().trim() );
            world_size_title    = tokens.nextToken().trim();
            world_size          = Integer.parseInt( tokens.nextToken().trim() );
            known_eventID_start_title      = tokens.nextToken().trim();
            known_eventID_start            = Integer.parseInt(
                                             tokens.nextToken().trim() );
            user_eventID_start_title       = tokens.nextToken().trim();
            user_eventID_start             = Integer.parseInt(
                                             tokens.nextToken().trim() );
            user_solo_eventID_start_title  = tokens.nextToken().trim();
            user_solo_eventID_start        = Integer.parseInt(
                                             tokens.nextToken().trim() );
            known_stateID_count_title      = tokens.nextToken().trim();
            known_stateID_count            = Integer.parseInt(
                                             tokens.nextToken().trim() );
            user_stateID_count_title       = tokens.nextToken().trim();
            user_stateID_count             = Integer.parseInt(
                                             tokens.nextToken().trim() );
            user_solo_eventID_count_title  = tokens.nextToken().trim();
            user_solo_eventID_count        = Integer.parseInt(
                                             tokens.nextToken().trim() );
        } catch ( NoSuchElementException err ) {
            err.printStackTrace();
            return false;
        } catch ( NumberFormatException err ) {
            err.printStackTrace();
            return false;
        }

        /* Set the const in (comm,rank) -> lineID transformation */
        LineID.setCommRank2LineIDxForm( world_size );

        return true;
    }

    public String  getVersionString()
    { return version; }

    public boolean isVersionMatched()
    { return version.equalsIgnoreCase( Const.VERSION ); }

    public boolean isVersionCompatible()
    {
        String   old_version;
        boolean  isCompatible;
        int      idx;

        isCompatible = false;
        for ( idx = 0;
              idx < Const.COMPAT_VERSIONS.length && !isCompatible;
              idx++ ) {
            old_version   = Const.COMPAT_VERSIONS[ idx ];
            isCompatible  = version.equalsIgnoreCase( old_version );
        }
        return isCompatible;
    }

    public boolean isBigEndian()
    { return is_big_endian; }

    public int getBlockSize()
    { return block_size; }

    public int getCommWorldSize()
    { return world_size; }

    public int getKnownEventIDStart()
    { return known_eventID_start; }

    public int getUserEventIDStart()
    { return user_eventID_start; }

    public int getUserSoloEventIDStart()
    { return user_solo_eventID_start; }

    public int getKnownStateIDCount()
    { return known_stateID_count; }

    public int getUserStateIDCount()
    { return user_stateID_count; }

    public int getUserSoloEventIDCount()
    { return user_solo_eventID_count; }

    public String toString()
    {
         return ( version + "\n"
                + is_big_endian_title + is_big_endian + "\n"
                + block_size_title + block_size + "\n"
                + num_blocks_title + num_blocks + "\n"
                + world_size_title + world_size + "\n"
                + known_eventID_start_title + known_eventID_start + "\n"
                + user_eventID_start_title + user_eventID_start + "\n"
                + user_solo_eventID_start_title + user_solo_eventID_start + "\n"
                + known_stateID_count_title + known_stateID_count + "\n"
                + user_stateID_count_title + user_stateID_count + "\n"
                + user_solo_eventID_count_title + user_solo_eventID_count + "\n"
                );
    }
}

/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.slog2;

public class Const
{
           static final String  SLOG2_ID       = "SLOG 2";
           static final String  version_ID     = SLOG2_ID + ".0.6";
           static final byte    INVALID_byte   = Byte.MIN_VALUE;
           static final short   INVALID_short  = Short.MIN_VALUE;
           static final int     INVALID_int    = Integer.MIN_VALUE;
           static final long    INVALID_long   = Long.MIN_VALUE;
           static final int     NULL_int       = 0;
           static final int     NULL_iaddr     = 0;
           static final long    NULL_fptr      = 0;
           static final float   INVALID_float  = Float.MIN_VALUE;
           static final double  INVALID_double = Double.MIN_VALUE;
           static final int     TRUE           = 1;
           static final int     FALSE          = 0;

    public static final short   NUM_LEAFS      = 2;
    public static final int     LEAF_BYTESIZE  = 65536;

    public static final String  VERSION_HISTORY =
                                  "- Version SLOG 2.0.0's node employs\n"
                                + "  decreasing endtime ordering.\n"
                                + "- Version SLOG 2.0.1's node employs\n"
                                + "  increasing starttime ordering.\n"
                                + "- Version SLOG 2.0.2 added preview data\n"
                                + "  in legend order to shadow states.\n"
                                + "- Version SLOG 2.0.3 changed preview data\n"
                                + "  from legend to inclusive ratio order.\n"
                                + "- Version SLOG 2.0.4 removed unused\n"
                                + "  category objects.\n"
                                + "- Version SLOG 2.0.5 expanded preview data\n"
                                + "  to include inclusive & exclusive ratios.\n"
                                + "- Version SLOG 2.0.6 expanded preview data\n"
                                + "  to include the count of real drawables.\n"
                                + "  \n"
                                + "- 2.0.1 viewer draws 2.0.0 logfile's\n"
                                + "  state nesting stack incorrectly!\n"
                                + "- 2.0.2 viewer cannot read 2.0.1 logfile.\n"
                                + "- 2.0.3 viewer draws 2.0.2 logfile's\n"
                                + "  preview weight incorrectly.\n"
                                + "- 2.0.4 viewer supports JOIN method and\n"
                                + "  can display 2.0.3 logfile correctly.\n"
                                + "- 2.0.5 viewer cannot read 2.0.4 logfile.\n"
                                + "- 2.0.6 viewer cannot read 2.0.5 logfile.\n";
}

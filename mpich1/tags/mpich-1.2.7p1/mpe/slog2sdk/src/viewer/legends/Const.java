/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package viewer.legends;

import java.awt.Color;

public class Const
{
    /*
       * ICON_WIDTH & ICON_HEIGHT need to be ODD numbers, so a center line
         in the icon is really centered in the middle of the icon. 
       * The ratio of ICON_HEIGHT to ICON_WIDTH should be no more than 0.80
         for appealing reason.
    */
           static final int    ICON_WIDTH                  = 35;
           static final int    ICON_HEIGHT                 = 21;

           static final int    CELL_HEIGHT                 = ICON_HEIGHT + 10;
           static final int    CELL_ICON_TEXT_GAP          = 8;
           static final Color  CELL_BACKCOLOR              = Color.lightGray;
           static final Color  CELL_FORECOLOR              = Color.black;
           static final Color  CELL_BACKCOLOR_SELECTED     = Color.gray;
           static final Color  CELL_FORECOLOR_SELECTED     = Color.yellow;

           static final int    LIST_MAX_VISIBLE_ROW_COUNT  = 25;
}

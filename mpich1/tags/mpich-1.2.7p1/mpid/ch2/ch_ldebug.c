#include <stdio.h>
#include "mpid.h"
#include "mpiddev.h"
#include "mpid_debug.h"

static char ch_debug_msgs[CH_LAST_DEBUG+1][CH_MAX_DEBUG_LINE];
static int  ch_msg_top = 0;
static int  ch_msg_bottom = 0;

/* This function is called by the macros in the mpid_debug.h file and the 
   function 'MPID_Ch_send_last_p4error' found at the end of this file.  It 
   checks to see if the top of the array is >= CH_LAST_DEBUG.  If this is true,
   then set the top back to 0 and the bottom to 1.  Then, copy the string 
   which was created in the macro to the appropriate place in the debug
   output array. */
void MPID_Print_last_args( char *msg )
{
    if ( ch_msg_top >= CH_LAST_DEBUG ) {
	ch_msg_top = 0;
	ch_msg_bottom = 1;
    }
    strcpy( ch_debug_msgs[ch_msg_top++], msg );
}

/*  This function prints to stderr the debug statements in the debug output
    array. */
void MPID_Ch_dprint_last( void )
{
    int i;
    static int in_call = 0;
    
    if (in_call) return;
    in_call = 1;
    if ( ch_msg_bottom == 1 ) {
	ch_msg_bottom = ch_msg_top;
    }
    i = ch_msg_bottom;
    do {
	fputs( ch_debug_msgs[i++], stderr );
	if ( i >= CH_LAST_DEBUG ) i = 0;
	} while ( i != ch_msg_top ); 
    in_call = 0;
}


/* This function processes the last p4 error. */
void MPID_Ch_send_last_p4error( char *p4_msg )
{
    int len;

    len = strlen(p4_msg);
    p4_msg[len] = '\0';
    p4_msg[len+1] = '\n';
    MPID_Print_last_args( p4_msg ); 
    MPID_Ch_dprint_last();
}
	





/***************************************************************************/
/* These are used to keep track of the number and kinds of messages that   */
/* are received                                                            */
/***************************************************************************/
#ifndef MPID_STAT_NONE
extern int MPID_n_short,         /* short messages */
           MPID_n_long,          /* long messages */
           MPID_n_unexpected,    /* unexpected messages */
           MPID_n_syncack;       /* Syncronization acknowledgments */
#endif
/***************************************************************************/

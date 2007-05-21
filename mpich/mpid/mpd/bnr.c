
/*------------------------------ mpd implementation of BNR interface ------------*/

#include "bnr.h"
#include "mpdlib.h"

BNR_Group bnr_allocate_group( int, int, int );
void bnr_deallocate_group( BNR_Group * );

int bnr_man_msgs_fd;
extern void (*MPD_user_peer_msg_handler)(char *);

volatile extern int MPD_global_fence_flag;

#define BNR_MAX_GROUPS 256

struct BNR_Group_struct {
    int id;
    int myrank;
    int size;
    int active;
    int open;			/* open = 1 means size not fixed yet */
};

BNR_Group bnr_initial_group;

struct BNR_Group_struct bnr_groups[BNR_MAX_GROUPS];

int bnr_open_groups[BNR_MAX_GROUPS];

int BNR_Get_group( BNR_Group *mygroup )
{
    *mygroup = bnr_initial_group;
    return BNR_SUCCESS;
}

BNR_Group bnr_allocate_group( int id, int myrank, int size )
{
    int i;

    for ( i = 0; i < BNR_MAX_GROUPS; i++ )
	if ( bnr_groups[i].active == 0 )
	    break;
    if ( i >= BNR_MAX_GROUPS ) {
	MPD_Printf( 1, "ran out of groups, BNR_MAX_GROUPS = %d\n", BNR_MAX_GROUPS );
	return NULL;
    }
    bnr_groups[i].active = 1;
    bnr_groups[i].id = id;
    bnr_groups[i].myrank = myrank;
    bnr_groups[i].size = size;
    bnr_groups[i].open = 0;	/* groups default to being closed (fixed size) */
    return( &bnr_groups[i] );
}

void bnr_deallocate_group( BNR_Group *group )
{
    (*group)->active = 0;
    *group = NULL;
}

int BNR_Init( )
{
    int i;

    MPD_global_fence_flag = 0;
    MPD_Init( MPD_user_peer_msg_handler );
    bnr_man_msgs_fd = MPD_Man_msgs_fd( );

    for ( i = 0; i < BNR_MAX_GROUPS; i++ )
	bnr_groups[i].active = 0;

    bnr_initial_group = bnr_allocate_group( 0 /* for now */, MPD_Rank( ), MPD_Size( ) );

    return(0);
}

int BNR_Kill( BNR_Group group )
{
    MPD_Abort( group->id );
    return( 0 );
}

int BNR_Fence( BNR_Group group )
/* barriers all processes in group; puts done before 
   the fence are accessible by gets after the fence */
{
    char buf[MPD_MAXLINE];
    int grank, gsize;
    
    BNR_Get_rank( group, &grank );
    BNR_Get_size( group, &gsize );
    sprintf( buf, "cmd=client_bnr_fence_in gid=%d grank=%d gsize=%d\n",
             group->id, grank, gsize );
    write( bnr_man_msgs_fd, buf, strlen(buf) ); /* check into fence */

    while ( !MPD_global_fence_flag ) /* use single fence flag for now */
	;	                 /* spin until set by interrupt-driven msg handler */
    MPD_global_fence_flag = 0;   /* Fixing a bug found by Weikuan, yuw@cis */
    return(0);
}

/* 
   For this mpd implementation of bnr, we always put the value on the
   local mpdman.  If 2 processes put the same key, it is possible for
   gets to retrieve different values for different processes.  Also,
   our current semantics is that a put is destructive, i.e. if you put
   a key that exists, it is overwritten.
*/
int BNR_Put( BNR_Group group, char *attr, char *val, int loc )
/* puts attr-value pair for retrieval by other processes in group;
   attr is a string of length < BNR_MAXATTRLEN;
   val is string of length < BNR_MAXVALLEN
   loc is an advisory-only suggested location */
{
    char buf[MPD_MAXLINE];
    char stuffed_attr[MPD_MAXLINE];
    char stuffed_val[MPD_MAXLINE];

    mpd_stuff_arg(attr,stuffed_attr);
    mpd_stuff_arg(val,stuffed_val);
    sprintf( buf, "cmd=client_bnr_put gid=%d attr=%s val=%s loc=%d\n",
	     group->id, stuffed_attr, stuffed_val, loc );
    write( bnr_man_msgs_fd, buf, strlen(buf) );
    return(0);
}

int BNR_Get( BNR_Group group, char *attr, char *val )
/* matches attr, retrieves corresponding value into val,
   which is a buffer of length = BNR_MAXVALLEN */
{
    int i;
    char buf[MPD_MAXLINE];
    char stuffed_attr[MPD_MAXLINE];
    char stuffed_val[MPD_MAXLINE];

    mpd_stuff_arg(attr,stuffed_attr);
    sprintf( buf, "cmd=client_bnr_get gid=%d attr=%s\n", group->id, stuffed_attr );
    write( bnr_man_msgs_fd, buf, strlen(buf) );
    i = mpd_read_line( bnr_man_msgs_fd, buf, MPD_MAXLINE );  
    MPD_Printf( 0, "BNRLIB len=%d bnr_get msg=>:%s:\n", i, buf );
    mpd_parse_keyvals( buf );
    mpd_getval( "cmd", buf );
    if ( strcmp( "client_bnr_get_output", buf ) == 0 ) {
        if ( ! mpd_getval( "val", stuffed_val ) )
            return(-1);
	mpd_destuff_arg(stuffed_val,val);
    }
    else if ( strcmp( "client_bnr_get_failed", buf ) == 0 ) {
        MPD_Printf( 1, "client_bnr_get failed\n", buf );
        return(-1);  /* not found */
    }
    else {
        MPD_Printf( 1, "expecting client_bnr_get_output; got :%s:\n", buf );
        return(-1);  /* not found */
    }
    return(0);
}

int BNR_Get_rank( BNR_Group group, int *myrank )
/* returns rank in group */
{
    *myrank = group->myrank;
    return 0;
}

int BNR_Get_size( BNR_Group group, int *groupsize )
/* returns size of group */
{
    *groupsize = group->size;
    return 0;
}

int BNR_Open_group( BNR_Group local_group, BNR_Group *new_group )
/* allocates a new group, with unique id */
{
    int rc, groupid;
    char c_groupid[8];
    static int open_group_cntr = 1;

    if ( local_group->myrank == 0 ) {
	groupid = ( local_group->id << 16 ) | open_group_cntr++;
	sprintf( c_groupid, "%d", groupid );
        BNR_Put( local_group, "new_group_id", c_groupid, -1 );
	MPD_Printf(0,"putting: new_group_id=%s\n",c_groupid);
    }
    BNR_Fence( local_group );
    rc = BNR_Get( local_group, "new_group_id", c_groupid );
    if (rc < 0)
    {
        MPD_Printf(1,"bnr_get failed for new_group_id\n");
	exit(-1);
    }
    MPD_Printf(0,"got: new_group_id=%s\n",c_groupid);

    /* Initially the new group is the same size as the old, and ranks are preserved */
    *new_group = bnr_allocate_group( atoi( c_groupid ), local_group->myrank,
				     local_group->size ); 
    (*new_group)->open = 1; 

    return( 0 );
}

int BNR_Close_group( BNR_Group group )
{
    group->open = 0;
    return(0);
}



/* The following parts of BNR are not yet implemented */

#if 0

int BNR_Spawn( BNR_gid group, int root, BNR_gid remote_group, char *command,
/* 	       char *argv[], char *env[], MPI_Info info, int array_of_errcodes[], */
	       int (notify_fn)(BNR_gid group, int rank, int exit_code) )
/* collective over group, arguments valid only at root.
   Note we pass *in* the new group id, assumed to have
   been BNR_allocated.  notify_fn is called if a process
   exits, and gets the group, rank, and return code.
   argv and env arrays are null terminated*/
{
    int i;

    for (i=0; i < og_idx; i++)
        if (bnr_open_groups[i] == (int) group)
	    break;
    if (i >= og_idx)  /* NOT found */
        return(-1);
    
    /* BODY OF SPAWN GOES HERE */
    return(0);
}

int BNR_merge( BNR_gid local_group, BNR_gid remote_group, BNR_gid *new_group )
/* calling process must be in the local group and must not be in
   the remote group.  Collective over the union of the two groups. */
{
}

int BNR_Spawn_multiple( BNR_gid group, int root, BNR_gid remote_group, int count,
			char *array_of_commands[], char *array_of_argv[],
/* 			char *array_of_env[], MPI_Info array_of_info[], */
			int array_of_errcodes[],
			int (notify_fn)(BNR_gid group, int rank, int exit_code) )
/* like BNR_Spawn, with arrays of length count */
{
}

int BNR_Get_parent( BNR_Group *parent_group )
{
}

int BNR_Free_group( BNR_Group group )
/* frees gid for re-use. */
{
}

#endif


/* ------------------------- backward compatibility for mpich-1 -----------------*/

int BNR_Pre_init( void (*peer_msg_handler)(char *) )
{
    MPD_user_peer_msg_handler = peer_msg_handler;
    return( 0 );
}

int BNR_Man_msgs_fd( int *fd )
{
    *fd = MPD_Man_msgs_fd();
    return( 0 );
}

int BNR_Poke_peer( int group_id, int dest_rank, char *msg )
{
    MPD_Poke_peer( group_id, dest_rank, msg );
    return( 0 );
}

int BNR_Get_group_id( BNR_Group group )
{
    return( group->id );
}

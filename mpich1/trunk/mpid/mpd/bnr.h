#ifndef BNR_H
#define BNR_H

#if defined(__cplusplus)
extern "C" {
#endif


#ifdef HAVE_MPICHBNR_API

#ifdef MPICHBNR_EXPORTS
#define MPICH_BNR_API __declspec(dllexport)
#else
#define MPICH_BNR_API __declspec(dllimport)
#endif

#else
#define MPICH_BNR_API
#endif


#ifdef HAVE_BNR_CALL
#define BNR_CALL __cdecl
#else
#define BNR_CALL
#endif


typedef struct BNR_Info_st {
    int cookie;
    char *key, *value;
    struct BNR_Info_struct *next;
} * BNR_Info;

typedef struct BNR_Group_struct * BNR_Group;

#define BNR_INFO_COOKIE		0x12345678
#define BNR_MAX_INFO_KEY	255
#define BNR_MAX_INFO_VAL	1024
#define BNR_INFO_NULL		((BNR_Info) 0)

#define BNR_INVALID_GROUP	((BNR_Group)0xffffffff)
#define BNR_GROUP_NULL		((BNR_Group)0)
#define BNR_SUCCESS			0
#define BNR_FAIL			-1
#define BNR_MAXATTRLEN		64
#define BNR_MAXVALLEN		3*1024

/*******************************************************
 * Construction / Destruction of the BNR interface
 *
 */

/* Initializes the bnr interface */
MPICH_BNR_API int BNR_Init( void );

/* frees any internal resources
 * No BNR calls may be made after BNR_Finalize
 */
MPICH_BNR_API int BNR_Finalize( void );


  /* ***
 *** New functions used in mpid/ch_p4mpd/p4mpd/lib/p4_bm.c
 *** */
int BNR_Pre_init( void ( * ) ( char * ) );
int BNR_Man_msgs_fd( int * );
int BNR_Poke_peer( int, int, char * );


/*******************************************************
 * Group management functions
 *
 */

/* returns primary group id assigned at creation */
MPICH_BNR_API int BNR_Get_group( BNR_Group *mygroup );

/* returns group that spawned this process or BNR_GROUP_NULL if no parent */
MPICH_BNR_API int BNR_Get_parent( BNR_Group *parent_group );

/* returns rank in group */
MPICH_BNR_API int BNR_Get_rank( BNR_Group group, int *myrank );

/* returns size of group */
MPICH_BNR_API int BNR_Get_size( BNR_Group group, int *size );

/* Allocates a new, unique group id which may be used 
 * in multiple spawn calls until it is closed.
 * Collective over the local group.
 * Cannot be fenced until after it has been closed.
 */
MPICH_BNR_API int BNR_Open_group( BNR_Group local_group, BNR_Group *new_group );

/* ???? */
MPICH_BNR_API int BNR_Get_group_id( BNR_Group group );

/* Closes an open group.
 * Collective over the group that opened it.
 */
MPICH_BNR_API int BNR_Close_group( BNR_Group group );

/* frees group for re-use. */
MPICH_BNR_API int BNR_Free_group( BNR_Group group );

/* Calling process must be in the local group and
 * must not be in the remote group.  Collective 
 * over the union of the two groups. 
 */
MPICH_BNR_API int BNR_Merge( 
    BNR_Group local_group, BNR_Group remote_group, BNR_Group *new_group );


/*******************************************************
 * Process management functions
 *
 */

/* not collective.
 * remote_group is an open BNR_Group and may be passed to Spawn 
 * multiple times. It is not valid until it is closed.  
 * BNR_Spawn will fail if remote_group is closed or uninitialized.
 * notify_fn is called if a process exits, and gets the
 * group, rank, and return code. argv and env
 * arrays are null terminated.  The caller's group is the
 * parent of the spawned processes.
 */

MPICH_BNR_API int BNR_Spawn( 
    BNR_Group remote_group, 
    int count, char *command, char *args, char *env, 
    BNR_Info info, int (*notify_fn)(BNR_Group group, 
    int rank, int exit_code) );

/* kills processes in group given by group.  This
 * can be used, for example, during spawn_multiple
 * when a spawn fails, to kill off groups already
 * spawned before returning failure 
 */
MPICH_BNR_API int BNR_Kill( BNR_Group group );


/*******************************************************
 * Attribute management functions
 *
 */

/* puts attr-value pair for retrieval by other
 * processes in group;  attr is a string of
 * length < BNR_MAXATTRLEN, val is string of
 * length < BNR_MAXVALLEN 
 * rank_advice tells BNR where the Get is likely to be called from.
 * rank_advice can be -1 for no advice.
 */
MPICH_BNR_API int BNR_Put( 
    BNR_Group group, char *attr, char *val, int rank_advice );

/* matches attr, retrieves corresponding value
 * into val, which is a buffer of
 * length = BNR_MAXVALLEN 
 */
MPICH_BNR_API int BNR_Get( BNR_Group group, char *attr, char *val );

/* barriers all processes in group; puts done
 * before the fence are accessible by gets after
 * the fence 
 */
MPICH_BNR_API int BNR_Fence( BNR_Group );	   


/*******************************************************
 * Global asynchronous put/get functions
 *
 * The following are needed for publishing.  They require no fence, since they
 * are not assumed to be either scalable or local.  The inevitable race 
 * condition is just accepted.
 */ 

/* deposits attr-value pair for access */
MPICH_BNR_API int BNR_Deposit( char *attr, char *value );

/* withdraws attr-value pair */
MPICH_BNR_API int BNR_Withdraw( char *attr, char *value );

/* finds value of attribute */
MPICH_BNR_API int BNR_Lookup( char *attr, char *value );


/********************************************************
 * BNR_Info modification functions
 *
 */

MPICH_BNR_API int BNR_Info_set(BNR_Info info, char *key, char *value);
MPICH_BNR_API int BNR_Info_get_valuelen(BNR_Info info, char *key, int *valuelen, int *flag);
MPICH_BNR_API int BNR_Info_get_nthkey(BNR_Info info, int n, char *key);
MPICH_BNR_API int BNR_Info_get_nkeys(BNR_Info info, int *nkeys);
MPICH_BNR_API int BNR_Info_get(BNR_Info info, char *key, int valuelen, char *value, int *flag);
MPICH_BNR_API int BNR_Info_free(BNR_Info *info);
MPICH_BNR_API int BNR_Info_dup(BNR_Info info, BNR_Info *newinfo);
MPICH_BNR_API int BNR_Info_delete(BNR_Info info, char *key);
MPICH_BNR_API int BNR_Info_create(BNR_Info *info);

#if defined(__cplusplus)
}
#endif 

#endif

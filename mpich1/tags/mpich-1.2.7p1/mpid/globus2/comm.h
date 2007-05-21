#ifndef MPICH_GLOBUS2_COMM_H
#define MPICH_GLOBUS2_COMM_H

#ifndef MPIR_GROUP_COOKIE


/**********************************************************************/
/* do not use the general collective functions defined in
 * src/coll/intra_fns.c or src/coll/intra_fns_new.c; use the ones
 * defined for globus2 device */

#include "topology_intra_fns.h"

/* SEBASTIEN: improve Alltoall to exchange less data (create better
 * tuned data types).
 *
 * Improve Reduce_scatter() in non-commutative case: in 1st phase
 * (gather to the roots), at each level: start posting all the send's
 * when I'm local slave, then post all the recv's when I'm local root. */

#define MPID_Barrier /* symmetric operation: check performance gain */
#define MPID_Bcast
#define MPID_Gather   /* comparing with and without packing/unpacking */
#undef MPID_Gatherv /* this function works: evaluate its performance */
#define MPID_Scatter   /* comparing with and without packing/unpacking */
#undef MPID_Scatterv /* not written yet: see MPID_Gatherv's performance 1st */
#define MPID_Allgather /* symmetric operation: check performance gain */
#define MPID_Allgatherv /* symmetric operation: check performance gain */
#undef MPID_Alltoall /* symmetric operation: check performance gain */
#undef MPID_Alltoallv /* same problem as MPI_Gatherv, in a worse version */
#define MPID_Reduce /* check perf for non-commutative case, with large msg */
#define MPID_Allreduce
#define MPID_Reduce_scatter   /* possible improvements */
#define MPID_Scan   /* compare performance with Rajeev's topol. UNaware */
/* The followings are MPI-2 */
#undef MPID_Alltoallw
#undef MPID_Exscan


/* some data structures used by the topology aware functions */

/* set of processes which will have to talk together at a given level */
typedef struct
{
   int size;               /* number of processes in the set */
   int root_index;         /* position of the root process in the set */
   int my_rank_index;      /* position of the current process in the set */
   int *set;               /* array of process ids in the set */
} comm_set_t;


/**********************************************************************/

/*
 * Definition of a communicator and group
 */
#define MPIR_GROUP_COOKIE 0xea01beaf
struct MPIR_GROUP {
    MPIR_COOKIE             /* Cookie to help detect valid item */
    int np;			        /* Number of processes in group */
    int local_rank;         /* My rank in the group (if I belong) */
    int ref_count;          /* Number of references to this group */
    int N2_next;            /* Next power of 2 from np */
    int N2_prev;            /* Previous power of 2 from np */
    int permanent;          /* Permanent group */
    int *lrank_to_grank;    /* Mapping from local to "global" ranks */
    int *set_mark;          /* Used for set marking/manipulation on groups */
    int self;               /* Index to MPI_Group for this item */
};

/*
 * Attributes
 */
typedef struct _MPIR_HBT *MPIR_HBT;
/* 
   Error handlers must survive being deleted and set to MPI_ERRHANDLER_NULL,
   the reference count is for knowing how many communicators still have this
   error handler active 
 */
struct MPIR_Errhandler {
    MPIR_COOKIE                    /* Cookie to help detect valid items */
    MPI_Handler_function *routine;
    int                  ref_count;
    };
#define MPIR_ERRHANDLER_COOKIE 0xe443a2dd

/* was unsigned long */
typedef  int MPIR_CONTEXT;
/* #define  MPIR_CONTEXT_TYPE MPI_UNSIGNED_LONG */
#define MPIR_CONTEXT_TYPE MPI_INT

#define  MPIR_WORLD_PT2PT_CONTEXT 0
#define  MPIR_WORLD_COLL_CONTEXT  1
#define  MPIR_SELF_PT2PT_CONTEXT  2
#define  MPIR_SELF_COLL_CONTEXT   3
#define  MPIR_FIRST_FREE_CONTEXT  4

typedef enum { MPIR_INTRA=1, MPIR_INTER } MPIR_COMM_TYPE;

typedef struct _MPIR_COLLOPS *MPIR_COLLOPS;
/*
   The local_rank field is used to reduce unnecessary memory references
   when doing send/receives.  It must equal local_group->local_rank.

   lrank_to_grank is group->lrank_to_grank; this is also used to 
   reduce memory refs.  (it is IDENTICAL, not just a copy; the "group"
   owns the array.)

   These have been ordered so that the most common elements are 
   near the top, in hopes of improving cache utilization.

   For a normal intra-communicator the group and local_group are identical
   The group differs from the local_group only in an inter-communicator
 */
#define MPIR_COMM_COOKIE 0xea02beaf
struct MPIR_COMMUNICATOR {
    MPIR_COOKIE                   /* Cookie to help detect valid item */
    /* Most common data from group is cached here */
    int           np;             /* size of (remote) group */
    int           local_rank;     /* rank in local_group of this process */
    int           *lrank_to_grank;/* mapping for group */
    MPIR_CONTEXT   send_context;  /* context to send messages */
    MPIR_CONTEXT   recv_context;  /* context to recv messages */
    void          *ADIctx;        /* Context (if any) for abstract device */

    /* This stuff is needed for the communicator implemenation, but less
       often than the above items */
    MPIR_COMM_TYPE comm_type;	  /* inter or intra */
    struct MPIR_GROUP *group;	  /* group associated with communicator */
    struct MPIR_GROUP *local_group;    /* local group */
    struct MPIR_COMMUNICATOR *comm_coll; 
                                  /* communicator for collective ops */
    int           self;           /* Index for external (MPI_Comm) value */
    int            ref_count;     /* number of references to communicator */
    void          *comm_cache;	  /* Hook for communicator cache */
    MPIR_HBT      attr_cache;     /* Hook for attribute cache */
    int           use_return_handler;   /* Allows us to override error_handler
					   when the MPI implementation
					   calls MPI routines */
    MPI_Errhandler error_handler;  /* Error handler */
    int            permanent;      /* Is this a permanent object? */
    void          *mutex;          /* Local for threaded versions */

    /*** BEGIN HETEROGENEOUS ONLY ***/
    MPID_Msg_pack_t  msgform;      /* Message representation form for 
				      ALL PROCESSES in this communicator */
    /* Note that point-to-point information on message representations
       is managed directly by the device and is not duplicated in the
       communicator */
    /*** END HETEROGENEOUS ONLY ***/

    /* These are used to support collective operations in this context */
    void          *adiCollCtx;
    MPIR_COLLOPS  collops;

    /*
     * These elements are used to store the vendor MPI communicator associated
     * with this communicator, and the mappings needed to ranks between the two
     * spaces.
     */
#   if defined(VMPI)
    void *	vmpi_comm;		/* ptr to vendor communicator */
    int *	lrank_to_vlrank;	/* mapping from local rank to
					   rank in vendor's communicator */
    int *	vlrank_to_lrank;	/* mapping from local vendor rank to
					   a rank in this communicator */
    int *	vgrank_to_vlrank;	/* mapping from global vendor rank to a
					   local rank in the vendor
					   communicator */
#   endif
    
    /* These are only required to allow debuggers a way to locate
     * all of the communicators in the code, and provide a print name
     * for each. (The user may be able to set this name, at some point).
     */
    struct MPIR_COMMUNICATOR *comm_next; /* A chain through all 
					    communicators */
    char 		     *comm_name; /* A print name for this 
					    communicator */

    globus_bool_t vmpi_only; /* to avoid polling TCP on MPI_SOURCE_ANY recvs
				when all procs in comm use vMPI
			      */

    /*** Topology aware stuff ***/
    int *Topology_Depths;   /* Depths[proc] */
    int **Topology_ClusterIds;   /* ClusterIds[proc][level] */
    int **Topology_Colors;   /* Colors[proc][level] */
    /* size of each cluster at each level: ClusterSizes[level][color] */
    int **Topology_ClusterSizes;
    /* array of communicating procs at each level */
    comm_set_t *Topology_CommSets;   /* CommSets[level] */
    /* 2D-array for the process ranks inside a cluster (at a given level) */
    int **Topology_Ranks;   /* Ranks[proc][level] */
};

/*
 * The list of all communicators in the program.
 */
typedef struct _MPIR_Comm_list {
  int	 	   	sequence_number;
  struct MPIR_COMMUNICATOR * comm_first;
} MPIR_Comm_list ;

extern MPIR_Comm_list MPIR_All_communicators;

/* Note that MPIR_ToPointer checks indices against limits */
#define MPIR_GET_COMM_PTR(idx) \
    (struct MPIR_COMMUNICATOR *)MPIR_ToPointer( idx )
#define MPIR_TEST_COMM_NOTOK(idx,ptr) \
   (!(ptr) || ((ptr)->cookie != MPIR_COMM_COOKIE))
#define MPIR_TEST_MPI_COMM(idx,ptr,comm,routine_name) \
{if (!(ptr)) {RETURNV(MPIR_ERROR(comm,MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_NULL),routine_name));}\
   if ((ptr)->cookie != MPIR_COMM_COOKIE){\
    mpi_errno=MPIR_Err_setmsg(MPI_ERR_COMM,MPIR_ERR_COMM_CORRUPT,routine_name,(char *)0,(char*)0,(ptr)->cookie);\
   RETURNV(MPIR_ERROR(comm,mpi_errno,routine_name));}}

#define MPIR_GET_GROUP_PTR(idx) \
    (struct MPIR_GROUP *)MPIR_ToPointer( idx )
#define MPIR_TEST_GROUP_NOTOK(idx,ptr) \
   (!(ptr) || ((ptr)->cookie != MPIR_GROUP_COOKIE))
#define MPIR_TEST_MPI_GROUP(idx,ptr,comm,routine_name) \
{if (!(ptr)) {RETURNV(MPIR_ERROR(comm,MPI_ERR_GROUP_NULL,routine_name));}\
   if ((ptr)->cookie != MPIR_GROUP_COOKIE){\
    MPIR_ERROR_PUSH_ARG(&(ptr)->cookie);\
   RETURNV(MPIR_ERROR(comm,MPI_ERR_GROUP_CORRUPT,routine_name));}}

#define MPIR_GET_ERRHANDLER_PTR(idx) \
    (struct MPIR_Errhandler *)MPIR_ToPointer( idx )
#define MPIR_TEST_ERRHANDLER_NOTOK(idx,ptr) \
   (!(ptr) || ((ptr)->cookie != MPIR_ERRHANDLER_COOKIE))
#define MPIR_TEST_MPI_ERRHANDLER(idx,ptr,comm,routine_name) \
{if (!(ptr)) {RETURNV(MPIR_ERROR(comm,MPI_ERR_ERRHANDLER_NULL,routine_name));}\
   if ((ptr)->cookie != MPIR_ERRHANDLER_COOKIE){\
    MPIR_ERROR_PUSH_ARG(&(ptr)->cookie);\
   RETURNV(MPIR_ERROR(comm,MPI_ERR_ERRHANDLER_CORRUPT,routine_name));}}
#endif

/*****************/
/* START GRIDFTP */
/*****************/

/* 
 * these overwrite default macros found in mpid/ch2/mpid.h 
 * any device that overwrites one must overwrite all three.
 *
 * vvvvvvvvvvvvv from mpid/ch2/mpid.h
 * These macros define an interface between the device and the rest of the 
 * MPI code for attributes.  If the device needs to use these, it *must*
 * define all 3.
 *
 * MPID_ATTR_SET(struct MPIR_COMMUNICATOR *comm, int keyval, void *attr_value)
 * is called when the user sets an attribute value for any keyval.
 *
 * MPID_ATTR_GET(struct MPIR_COMMUNICATOR *comm, int keyval, void *attr_value)
 * is called when the user gets an attribute value The last argument is a 
 * pointer to a value, not a pointer to a pointer (store into the storage
 * defined by the user, don't change the pointer)
 *
 * MPID_KEYVAL_INIT()
 * The device should also call MPI_Keyval_create() for any keyvals that 
 * it wishes to be available for users.  Otherwise, the keyvals will have
 * value MPI_KEYVAL_INVALID.
 * ^^^^^^^^^^^^^ from mpid/ch2/mpid.h
 */

#undef MPID_ATTR_SET
#undef MPID_ATTR_GET
#undef MPID_KEYVAL_INIT
#define MPID_ATTR_SET(a,b,c) MPID_Attr_set((a), (b), (c));
#define MPID_ATTR_GET(comm_ptr,keyval,attr_value_p) 
#define MPID_KEYVAL_INIT()                  \
     MPI_Keyval_create(MPI_NULL_COPY_FN,    \
			MPI_NULL_DELETE_FN, \
			&MPICHX_PARALLELSOCKETS_PARAMETERS,    \
			(void *) 0);
/***************/
/* END GRIDFTP */
/***************/

/*
 * Function prototypes
 */
int MPID_Comm_init(
    struct MPIR_COMMUNICATOR *		oldcomm,
    struct MPIR_COMMUNICATOR *		newcomm);

int MPID_Comm_free(
    struct MPIR_COMMUNICATOR *		comm);

void MPID_ZeroStatusCount(
    MPI_Status *			status);

#endif   /* MPICH_GLOBUS2_COMM_H */


/*
 *  $Id: mpi.h,v 1.47 2005/02/12 14:48:03 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

/* user include file for MPI programs */

#ifndef _MPI_INCLUDE
#define _MPI_INCLUDE

/* needed for MPI-2 extensions */
#define MPI_MAX_PORT_NAME 256

/* 
   NEW_POINTERS enables using ints for the MPI objects instead of addresses.
   This is required by MPI 1.1, particularly for Fortran (also for C, 
   since you can now have "constant" values for things like MPI_INT, and
   they are stronger than "constant between MPI_INIT and MPI_FINALIZE".
   For example, we might want to allow MPI_INT as a case label.

   NEW_POINTERS is the default; I'm purging all code that doesn't use
   NEW_POINTERS from the source.
 */
#define NEW_POINTERS

/* Keep C++ compilers from getting confused */
#if defined(__cplusplus)
extern "C" {
#endif

/* Results of the compare operations */
/* These should stay ordered */
#define MPI_IDENT     0
#define MPI_CONGRUENT 1
#define MPI_SIMILAR   2
#define MPI_UNEQUAL   3

/* Data types
 * A more aggressive yet homogeneous implementation might want to 
 * make the values here the number of bytes in the basic type, with
 * a simple test against a max limit (e.g., 16 for long double), and
 * non-contiguous structures with indices greater than that.
 * 
 * Note: Configure knows these values for providing the Fortran optional
 * types (like MPI_REAL8).  Any changes here must be matched by changes
 * in configure.in
 */
typedef int MPI_Datatype;
#define MPI_CHAR           ((MPI_Datatype)1)
#define MPI_UNSIGNED_CHAR  ((MPI_Datatype)2)
#define MPI_BYTE           ((MPI_Datatype)3)
#define MPI_SHORT          ((MPI_Datatype)4)
#define MPI_UNSIGNED_SHORT ((MPI_Datatype)5)
#define MPI_INT            ((MPI_Datatype)6)
#define MPI_UNSIGNED       ((MPI_Datatype)7)
#define MPI_LONG           ((MPI_Datatype)8)
#define MPI_UNSIGNED_LONG  ((MPI_Datatype)9)
#define MPI_FLOAT          ((MPI_Datatype)10)
#define MPI_DOUBLE         ((MPI_Datatype)11)
#define MPI_LONG_DOUBLE    ((MPI_Datatype)12)
#define MPI_LONG_LONG_INT  ((MPI_Datatype)13)
/* MPI_LONG_LONG is in the complete ref 2nd edition, though not in the 
   standard.  Rather, MPI_LONG_LONG_INT is on page 40 in the HPCA version */
#define MPI_LONG_LONG      ((MPI_Datatype)13)

#define MPI_PACKED         ((MPI_Datatype)14)
#define MPI_LB             ((MPI_Datatype)15)
#define MPI_UB             ((MPI_Datatype)16)

/* 
   The layouts for the types MPI_DOUBLE_INT etc are simply
   struct { 
       double var;
       int    loc;
   }
   This is documented in the man pages on the various datatypes.   
 */
#define MPI_FLOAT_INT         ((MPI_Datatype)17)
#define MPI_DOUBLE_INT        ((MPI_Datatype)18)
#define MPI_LONG_INT          ((MPI_Datatype)19)
#define MPI_SHORT_INT         ((MPI_Datatype)20)
#define MPI_2INT              ((MPI_Datatype)21)
#define MPI_LONG_DOUBLE_INT   ((MPI_Datatype)22)

/* Fortran types */
#define MPI_COMPLEX           ((MPI_Datatype)23)
#define MPI_DOUBLE_COMPLEX    ((MPI_Datatype)24)
#define MPI_LOGICAL           ((MPI_Datatype)25)
#define MPI_REAL              ((MPI_Datatype)26)
#define MPI_DOUBLE_PRECISION  ((MPI_Datatype)27)
#define MPI_INTEGER           ((MPI_Datatype)28)
#define MPI_2INTEGER          ((MPI_Datatype)29)
#define MPI_2COMPLEX          ((MPI_Datatype)30)
#define MPI_2DOUBLE_COMPLEX   ((MPI_Datatype)31)
#define MPI_2REAL             ((MPI_Datatype)32)
#define MPI_2DOUBLE_PRECISION ((MPI_Datatype)33)
#define MPI_CHARACTER         ((MPI_Datatype)1)

/* Communicators */
typedef int MPI_Comm;
#define MPI_COMM_WORLD 91
#define MPI_COMM_SELF  92

/* Groups */
typedef int MPI_Group;
#define MPI_GROUP_EMPTY 90

/* Collective operations */
typedef int MPI_Op;

#define MPI_MAX    (MPI_Op)(100)
#define MPI_MIN    (MPI_Op)(101)
#define MPI_SUM    (MPI_Op)(102)
#define MPI_PROD   (MPI_Op)(103)
#define MPI_LAND   (MPI_Op)(104)
#define MPI_BAND   (MPI_Op)(105)
#define MPI_LOR    (MPI_Op)(106)
#define MPI_BOR    (MPI_Op)(107)
#define MPI_LXOR   (MPI_Op)(108)
#define MPI_BXOR   (MPI_Op)(109)
#define MPI_MINLOC (MPI_Op)(110)
#define MPI_MAXLOC (MPI_Op)(111)

/* Permanent key values */
/* C Versions (return pointer to value) */
#define MPI_TAG_UB           81
#define MPI_HOST             83
#define MPI_IO               85
#define MPI_WTIME_IS_GLOBAL  87
/* Fortran Versions (return integer value) */
#define MPIR_TAG_UB          80
#define MPIR_HOST            82
#define MPIR_IO              84
#define MPIR_WTIME_IS_GLOBAL 86

/* Define some null objects */
#define MPI_COMM_NULL      ((MPI_Comm)0)
#define MPI_OP_NULL        ((MPI_Op)0)
#define MPI_GROUP_NULL     ((MPI_Group)0)
#define MPI_DATATYPE_NULL  ((MPI_Datatype)0)
#define MPI_REQUEST_NULL   ((MPI_Request)0)
#define MPI_ERRHANDLER_NULL ((MPI_Errhandler )0)

/* These are only guesses; make sure you change them in mpif.h as well */
#define MPI_MAX_PROCESSOR_NAME 256
#define MPI_MAX_ERROR_STRING   512
#define MPI_MAX_NAME_STRING     63		/* How long a name do you need ? */

/* Pre-defined constants */
#define MPI_UNDEFINED      (-32766)
#define MPI_UNDEFINED_RANK MPI_UNDEFINED
#define MPI_KEYVAL_INVALID 0

/* Upper bound on the overhead in bsend for each message buffer */
#define MPI_BSEND_OVERHEAD 512

/* Topology types */
#define MPI_GRAPH  1
#define MPI_CART   2

#define MPI_BOTTOM      (void *)0

#define MPI_PROC_NULL   (-1)
#define MPI_ANY_SOURCE 	(-2)
#define MPI_ROOT        (-3)
#define MPI_ANY_TAG     (-1)

#define MPI_ERRORS_ARE_FATAL ((MPI_Errhandler)119)
#define MPI_ERRORS_RETURN    ((MPI_Errhandler)120)
#define MPIR_ERRORS_WARN     ((MPI_Errhandler)121)
typedef int MPI_Errhandler;
/* Make the C names for the null functions all upper-case.  Note that 
   this is required for systems that use all uppercase names for Fortran 
   externals.  */
/* MPI 1 names */
#define MPI_NULL_COPY_FN   MPIR_null_copy_fn
#define MPI_NULL_DELETE_FN MPIR_null_delete_fn
#define MPI_DUP_FN         MPIR_dup_fn
/* MPI 2 names */
#define MPI_COMM_NULL_COPY_FN MPI_NULL_COPY_FN
#define MPI_COMM_NULL_DELETE_FN MPI_NULL_DELETE_FN
#define MPI_COMM_DUP_FN MPI_DUP_FN

/* MPI request opjects */
typedef union MPIR_HANDLE *MPI_Request;

/* User combination function */
typedef void (MPI_User_function) ( void *, void *, int *, MPI_Datatype * ); 

/* MPI Attribute copy and delete functions */
typedef int (MPI_Copy_function) ( MPI_Comm, int, void *, void *, void *, int * );
typedef int (MPI_Delete_function) ( MPI_Comm, int, void *, void * );

#define MPI_VERSION    1
#define MPI_SUBVERSION 2
#define MPICH_NAME     1
#define MPICH_VERSION "1.2.7"

/********************** MPI-2 FEATURES BEGIN HERE ***************************/
#define MPICH_HAS_C2F

/* for the datatype decoders */
#define MPI_COMBINER_NAMED         2312
#define MPI_COMBINER_CONTIGUOUS    2313
#define MPI_COMBINER_VECTOR        2314
#define MPI_COMBINER_HVECTOR       2315
#define MPI_COMBINER_INDEXED       2316
#define MPI_COMBINER_HINDEXED      2317
#define MPI_COMBINER_STRUCT        2318

/* for info */
typedef struct MPIR_Info *MPI_Info;
# define MPI_INFO_NULL         ((MPI_Info) 0)
# define MPI_MAX_INFO_KEY       255
# define MPI_MAX_INFO_VAL      1024

/* for subarray and darray constructors */
#define MPI_ORDER_C              56
#define MPI_ORDER_FORTRAN        57
#define MPI_DISTRIBUTE_BLOCK    121
#define MPI_DISTRIBUTE_CYCLIC   122
#define MPI_DISTRIBUTE_NONE     123
#define MPI_DISTRIBUTE_DFLT_DARG -49767

/* mpidefs.h includes configuration-specific information, such as the 
   type of MPI_Aint or MPI_Fint, also mpio.h, if it was built.  
   It now includes the definition of MPI_Status */
#include "mpidefs.h"

/* MPI Error handlers.  Systems that don't support stdargs can't use
   this definition (this must occur after the mpidefs.h include,
   since USE_STDARG is set in that file)
 */
#if defined(USE_STDARG) 
typedef void (MPI_Handler_function) ( MPI_Comm *, int *, ... );
#else
typedef void (MPI_Handler_function) ();
#endif


/* Handle conversion types/functions */

/* Programs that need to convert types used in MPICH should use these */
#define MPI_Comm_c2f(comm) (MPI_Fint)(comm)
#define MPI_Comm_f2c(comm) (MPI_Comm)(comm)
#define MPI_Type_c2f(datatype) (MPI_Fint)(datatype)
#define MPI_Type_f2c(datatype) (MPI_Datatype)(datatype)
#define MPI_Group_c2f(group) (MPI_Fint)(group)
#define MPI_Group_f2c(group) (MPI_Group)(group)
/* MPI_Request_c2f is a routine in src/misc2 */
#define MPI_Request_f2c(request) (MPI_Request)MPIR_ToPointer(request)
#define MPI_Op_c2f(op) (MPI_Fint)(op)
#define MPI_Op_f2c(op) (MPI_Op)(op)
#define MPI_Errhandler_c2f(errhandler) (MPI_Fint)(errhandler)
#define MPI_Errhandler_f2c(errhandler) (MPI_Errhandler)(errhandler)

/* For new MPI-2 types */
#define MPI_Win_c2f(win)   (MPI_Fint)(win)
#define MPI_Win_f2c(win)   (MPI_Win)(win)

#define MPI_STATUS_IGNORE (MPI_Status *)0
#define MPI_STATUSES_IGNORE (MPI_Status *)0

/* For supported thread levels */
#define MPI_THREAD_SINGLE 0
#define MPI_THREAD_FUNNELED 1
#define MPI_THREAD_SERIALIZED 2
#define MPI_THREAD_MULTIPLE 3

/********************** MPI-2 FEATURES END HERE ***************************/

/*************** Experimental MPICH FEATURES BEGIN HERE *******************/
/* Attribute keys.  These are set to MPI_KEYVAL_INVALID by default */
extern int MPICHX_QOS_BANDWIDTH;
extern int MPICHX_QOS_PARAMETERS;
/**************** Experimental MPICH FEATURES END HERE ********************/

/******** Globus-2 device gives access to the underlying topology *********/

/* get the underlying topology at the user level using attribute caching */
extern int MPICHX_TOPOLOGY_DEPTHS;
extern int MPICHX_TOPOLOGY_COLORS;

/* Name the different topology levels (the order is relevant!) */
#define MPICHX_WAN_LEVEL 0    /* communicate thru WAN TCP links */
#define MPICHX_LAN_LEVEL 1    /* communicate thru LAN TCP links */
#define MPICHX_HOST_LEVEL 2   /* communicate inside a machine thru TCP */
#define MPICHX_VMPI_LEVEL 3   /* use vendor-MPI library */

/* START GRIDFTP */
/******** Globus-2 device use of GridFTP ********/
/* Attribute keys.  These are set to MPI_KEYVAL_INVALID by default */
extern int MPICHX_PARALLELSOCKETS_PARAMETERS;
struct gridftp_params
{
    int partner_rank;

    int nsocket_pairs;
    int max_outstanding_writes;
    int tcp_buffsize;
}; /* end struct gridftp_params */
/* END GRIDFTP */

/******** End of Globus-2 device keys *************************************/

/* MPI's error classes */
#include "mpi_errno.h"
/* End of MPI's error classes */

/*
 * Normally, we provide prototypes for all MPI routines.  In a few wierd
 * cases, we need to suppress the prototypes.
 */
#ifndef MPICH_SUPPRESS_PROTOTYPES
/* We require that the C compiler support prototypes */
int MPI_Send(void*, int, MPI_Datatype, int, int, MPI_Comm);
int MPI_Recv(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Status *);
int MPI_Get_count(MPI_Status *, MPI_Datatype, int *);
int MPI_Bsend(void*, int, MPI_Datatype, int, int, MPI_Comm);
int MPI_Ssend(void*, int, MPI_Datatype, int, int, MPI_Comm);
int MPI_Rsend(void*, int, MPI_Datatype, int, int, MPI_Comm);
int MPI_Buffer_attach( void*, int);
int MPI_Buffer_detach( void*, int*);
int MPI_Isend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Ibsend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Issend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Irsend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Irecv(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Wait(MPI_Request *, MPI_Status *);
int MPI_Test(MPI_Request *, int *, MPI_Status *);
int MPI_Request_free(MPI_Request *);
int MPI_Waitany(int, MPI_Request *, int *, MPI_Status *);
int MPI_Testany(int, MPI_Request *, int *, int *, MPI_Status *);
int MPI_Waitall(int, MPI_Request *, MPI_Status *);
int MPI_Testall(int, MPI_Request *, int *, MPI_Status *);
int MPI_Waitsome(int, MPI_Request *, int *, int *, MPI_Status *);
int MPI_Testsome(int, MPI_Request *, int *, int *, MPI_Status *);
int MPI_Iprobe(int, int, MPI_Comm, int *flag, MPI_Status *);
int MPI_Probe(int, int, MPI_Comm, MPI_Status *);
int MPI_Cancel(MPI_Request *);
int MPI_Test_cancelled(MPI_Status *, int *);
int MPI_Send_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Bsend_init(void*, int, MPI_Datatype, int,int, MPI_Comm, MPI_Request *);
int MPI_Ssend_init(void*, int, MPI_Datatype, int,int, MPI_Comm, MPI_Request *);
int MPI_Rsend_init(void*, int, MPI_Datatype, int,int, MPI_Comm, MPI_Request *);
int MPI_Recv_init(void*, int, MPI_Datatype, int,int, MPI_Comm, MPI_Request *);
int MPI_Start(MPI_Request *);
int MPI_Startall(int, MPI_Request *);
int MPI_Sendrecv(void *, int, MPI_Datatype,int, int, void *, int, MPI_Datatype, int, int, MPI_Comm, MPI_Status *);
int MPI_Sendrecv_replace(void*, int, MPI_Datatype, int, int, int, int, MPI_Comm, MPI_Status *);
int MPI_Type_contiguous(int, MPI_Datatype, MPI_Datatype *);
int MPI_Type_vector(int, int, int, MPI_Datatype, MPI_Datatype *);
int MPI_Type_hvector(int, int, MPI_Aint, MPI_Datatype, MPI_Datatype *);
int MPI_Type_indexed(int, int *, int *, MPI_Datatype, MPI_Datatype *);
int MPI_Type_hindexed(int, int *, MPI_Aint *, MPI_Datatype, MPI_Datatype *);
int MPI_Type_struct(int, int *, MPI_Aint *, MPI_Datatype *, MPI_Datatype *);
int MPI_Address(void*, MPI_Aint *);
int MPI_Type_extent(MPI_Datatype, MPI_Aint *);
/* See the 1.1 version of the Standard; I think that the standard is in 
   error; however, it is the standard */
/* int MPI_Type_size(MPI_Datatype, MPI_Aint *size); */
int MPI_Type_size(MPI_Datatype, int *);
/* MPI_Type_count was withdrawn in MPI 1.1 */
int MPI_Type_lb(MPI_Datatype, MPI_Aint*);
int MPI_Type_ub(MPI_Datatype, MPI_Aint*);
int MPI_Type_commit(MPI_Datatype *);
int MPI_Type_free(MPI_Datatype *);
int MPI_Get_elements(MPI_Status *, MPI_Datatype, int *);
int MPI_Pack(void*, int, MPI_Datatype, void *, int, int *,  MPI_Comm);
int MPI_Unpack(void*, int, int *, void *, int, MPI_Datatype, MPI_Comm);
int MPI_Pack_size(int, MPI_Datatype, MPI_Comm, int *);
int MPI_Barrier(MPI_Comm );
int MPI_Bcast(void*, int, MPI_Datatype, int, MPI_Comm );
int MPI_Gather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, int, MPI_Comm); 
int MPI_Gatherv(void* , int, MPI_Datatype, void*, int *, int *, MPI_Datatype, int, MPI_Comm); 
int MPI_Scatter(void* , int, MPI_Datatype, void*, int, MPI_Datatype, int, MPI_Comm);
int MPI_Scatterv(void* , int *, int *,  MPI_Datatype, void*, int, MPI_Datatype, int, MPI_Comm);
int MPI_Allgather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, MPI_Comm);
int MPI_Allgatherv(void* , int, MPI_Datatype, void*, int *, int *, MPI_Datatype, MPI_Comm);
int MPI_Alltoall(void* , int, MPI_Datatype, void*, int, MPI_Datatype, MPI_Comm);
int MPI_Alltoallv(void* , int *, int *, MPI_Datatype, void*, int *, int *, MPI_Datatype, MPI_Comm);
int MPI_Reduce(void* , void*, int, MPI_Datatype, MPI_Op, int, MPI_Comm);
int MPI_Op_create(MPI_User_function *, int, MPI_Op *);
int MPI_Op_free( MPI_Op *);
int MPI_Allreduce(void* , void*, int, MPI_Datatype, MPI_Op, MPI_Comm);
int MPI_Reduce_scatter(void* , void*, int *, MPI_Datatype, MPI_Op, MPI_Comm);
int MPI_Scan(void* , void*, int, MPI_Datatype, MPI_Op, MPI_Comm );
int MPI_Group_size(MPI_Group group, int *);
int MPI_Group_rank(MPI_Group group, int *);
int MPI_Group_translate_ranks (MPI_Group, int, int *, MPI_Group, int *);
int MPI_Group_compare(MPI_Group, MPI_Group, int *);
int MPI_Comm_group(MPI_Comm, MPI_Group *);
int MPI_Group_union(MPI_Group, MPI_Group, MPI_Group *);
int MPI_Group_intersection(MPI_Group, MPI_Group, MPI_Group *);
int MPI_Group_difference(MPI_Group, MPI_Group, MPI_Group *);
int MPI_Group_incl(MPI_Group group, int, int *, MPI_Group *);
int MPI_Group_excl(MPI_Group group, int, int *, MPI_Group *);
int MPI_Group_range_incl(MPI_Group group, int, int [][3], MPI_Group *);
int MPI_Group_range_excl(MPI_Group group, int, int [][3], MPI_Group *);
int MPI_Group_free(MPI_Group *);
int MPI_Comm_size(MPI_Comm, int *);
int MPI_Comm_rank(MPI_Comm, int *);
int MPI_Comm_compare(MPI_Comm, MPI_Comm, int *);
int MPI_Comm_dup(MPI_Comm, MPI_Comm *);
int MPI_Comm_create(MPI_Comm, MPI_Group, MPI_Comm *);
int MPI_Comm_split(MPI_Comm, int, int, MPI_Comm *);
int MPI_Comm_free(MPI_Comm *);
int MPI_Comm_test_inter(MPI_Comm, int *);
int MPI_Comm_remote_size(MPI_Comm, int *);
int MPI_Comm_remote_group(MPI_Comm, MPI_Group *);
int MPI_Intercomm_create(MPI_Comm, int, MPI_Comm, int, int, MPI_Comm * );
int MPI_Intercomm_merge(MPI_Comm, int, MPI_Comm *);
int MPI_Keyval_create(MPI_Copy_function *, MPI_Delete_function *, int *, void*);
int MPI_Keyval_free(int *);
int MPI_Attr_put(MPI_Comm, int, void*);
int MPI_Attr_get(MPI_Comm, int, void *, int *);
int MPI_Attr_delete(MPI_Comm, int);
int MPI_Topo_test(MPI_Comm, int *);
int MPI_Cart_create(MPI_Comm, int, int *, int *, int, MPI_Comm *);
int MPI_Dims_create(int, int, int *);
int MPI_Graph_create(MPI_Comm, int, int *, int *, int, MPI_Comm *);
int MPI_Graphdims_get(MPI_Comm, int *, int *);
int MPI_Graph_get(MPI_Comm, int, int, int *, int *);
int MPI_Cartdim_get(MPI_Comm, int *);
int MPI_Cart_get(MPI_Comm, int, int *, int *, int *);
int MPI_Cart_rank(MPI_Comm, int *, int *);
int MPI_Cart_coords(MPI_Comm, int, int, int *);
int MPI_Graph_neighbors_count(MPI_Comm, int, int *);
int MPI_Graph_neighbors(MPI_Comm, int, int, int *);
int MPI_Cart_shift(MPI_Comm, int, int, int *, int *);
int MPI_Cart_sub(MPI_Comm, int *, MPI_Comm *);
int MPI_Cart_map(MPI_Comm, int, int *, int *, int *);
int MPI_Graph_map(MPI_Comm, int, int *, int *, int *);
int MPI_Get_processor_name(char *, int *);
int MPI_Get_version(int *, int *);
int MPI_Errhandler_create(MPI_Handler_function *, MPI_Errhandler *);
int MPI_Errhandler_set(MPI_Comm, MPI_Errhandler);
int MPI_Errhandler_get(MPI_Comm, MPI_Errhandler *);
int MPI_Errhandler_free(MPI_Errhandler *);
int MPI_Error_string(int, char *, int *);
int MPI_Error_class(int, int *);
double MPI_Wtime(void);
double MPI_Wtick(void);
#ifndef MPI_Wtime
double PMPI_Wtime(void);
double PMPI_Wtick(void);
#endif
int MPI_Init(int *, char ***);
int MPI_Init_thread( int *, char ***, int, int * );
int MPI_Finalize(void);
int MPI_Initialized(int *);
int MPI_Abort(MPI_Comm, int);

/* MPI-2 communicator naming functions */
int MPI_Comm_set_name(MPI_Comm, char *);
int MPI_Comm_get_name(MPI_Comm, char *, int *);

#ifdef HAVE_NO_C_CONST
/* Default Solaris compiler does not accept const but does accept prototypes */
#if defined(USE_STDARG) 
int MPI_Pcontrol(int, ...);
#else
int MPI_Pcontrol(int);
#endif
#else
int MPI_Pcontrol(const int, ...);
#endif

int MPI_NULL_COPY_FN ( MPI_Comm, int, void *, void *, void *, int * );
int MPI_NULL_DELETE_FN ( MPI_Comm, int, void *, void * );
int MPI_DUP_FN ( MPI_Comm, int, void *, void *, void *, int * );

/* misc2 (MPI2) */
int MPI_Status_f2c( MPI_Fint *, MPI_Status * );
int MPI_Status_c2f( MPI_Status *, MPI_Fint * );
int MPI_Finalized( int * );
int MPI_Type_create_indexed_block(int, int, int *, MPI_Datatype, MPI_Datatype *);
int MPI_Type_get_envelope(MPI_Datatype, int *, int *, int *, int *); 
int MPI_Type_get_contents(MPI_Datatype, int, int, int, int *, MPI_Aint *, MPI_Datatype *);
int MPI_Type_create_subarray(int, int *, int *, int *, int, MPI_Datatype, MPI_Datatype *);
int MPI_Type_create_darray(int, int, int, int *, int *, int *, int *, int, MPI_Datatype, MPI_Datatype *);
int MPI_Info_create(MPI_Info *);
int MPI_Info_set(MPI_Info, char *, char *);
int MPI_Info_delete(MPI_Info, char *);
int MPI_Info_get(MPI_Info, char *, int, char *, int *);
int MPI_Info_get_valuelen(MPI_Info, char *, int *, int *);
int MPI_Info_get_nkeys(MPI_Info, int *);
int MPI_Info_get_nthkey(MPI_Info, int, char *);
int MPI_Info_dup(MPI_Info, MPI_Info *);
int MPI_Info_free(MPI_Info *info);

MPI_Fint MPI_Info_c2f(MPI_Info);
MPI_Info MPI_Info_f2c(MPI_Fint);
MPI_Fint MPI_Request_c2f( MPI_Request );

/* external */
int MPI_Status_set_cancelled( MPI_Status *, int );
int MPI_Status_set_elements( MPI_Status *, MPI_Datatype, int );

#endif /* MPICH_SUPPRESS_PROTOTYPES */



/* Here are the bindings of the profiling routines */
#if !defined(MPI_BUILD_PROFILING)
int PMPI_Send(void*, int, MPI_Datatype, int, int, MPI_Comm);
int PMPI_Recv(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Status *);
int PMPI_Get_count(MPI_Status *, MPI_Datatype, int *);
int PMPI_Bsend(void*, int, MPI_Datatype, int, int, MPI_Comm);
int PMPI_Ssend(void*, int, MPI_Datatype, int, int, MPI_Comm);
int PMPI_Rsend(void*, int, MPI_Datatype, int, int, MPI_Comm);
int PMPI_Buffer_attach( void* buffer, int);
int PMPI_Buffer_detach( void* buffer, int*);
int PMPI_Isend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Ibsend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Issend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Irsend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Irecv(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Wait(MPI_Request *, MPI_Status *);
int PMPI_Test(MPI_Request *, int *flag, MPI_Status *);
int PMPI_Request_free(MPI_Request *);
int PMPI_Waitany(int, MPI_Request *, int *, MPI_Status *);
int PMPI_Testany(int, MPI_Request *, int *, int *, MPI_Status *);
int PMPI_Waitall(int, MPI_Request *, MPI_Status *);
int PMPI_Testall(int, MPI_Request *, int *flag, MPI_Status *);
int PMPI_Waitsome(int, MPI_Request *, int *, int *, MPI_Status *);
int PMPI_Testsome(int, MPI_Request *, int *, int *, MPI_Status *);
int PMPI_Iprobe(int, int, MPI_Comm, int *flag, MPI_Status *);
int PMPI_Probe(int, int, MPI_Comm, MPI_Status *);
int PMPI_Cancel(MPI_Request *);
int PMPI_Test_cancelled(MPI_Status *, int *flag);
int PMPI_Send_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Bsend_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Ssend_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Rsend_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Recv_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int PMPI_Start(MPI_Request *);
int PMPI_Startall(int, MPI_Request *);
int PMPI_Sendrecv(void *, int, MPI_Datatype, int, int, void *, int, MPI_Datatype, int, int, MPI_Comm, MPI_Status *);
int PMPI_Sendrecv_replace(void*, int, MPI_Datatype, int, int, int, int, MPI_Comm, MPI_Status *);
int PMPI_Type_contiguous(int, MPI_Datatype, MPI_Datatype *);
int PMPI_Type_vector(int, int, int, MPI_Datatype, MPI_Datatype *);
int PMPI_Type_hvector(int, int, MPI_Aint, MPI_Datatype, MPI_Datatype *);
int PMPI_Type_indexed(int, int *, int *, MPI_Datatype, MPI_Datatype *);
int PMPI_Type_hindexed(int, int *, MPI_Aint *, MPI_Datatype, MPI_Datatype *);
int PMPI_Type_struct(int, int *, MPI_Aint *, MPI_Datatype *, MPI_Datatype *);
int PMPI_Address(void*, MPI_Aint *);
int PMPI_Type_extent(MPI_Datatype, MPI_Aint *);
/* See the 1.1 version of the Standard; I think that the standard is in 
   error; however, it is the standard */
/* int PMPI_Type_size(MPI_Datatype, MPI_Aint *); */
int PMPI_Type_size(MPI_Datatype, int *);
int PMPI_Type_lb(MPI_Datatype, MPI_Aint*);
int PMPI_Type_ub(MPI_Datatype, MPI_Aint*);
int PMPI_Type_commit(MPI_Datatype *);
int PMPI_Type_free(MPI_Datatype *);
int PMPI_Get_elements(MPI_Status *, MPI_Datatype, int *);
int PMPI_Pack(void*, int, MPI_Datatype, void *, int, int *,  MPI_Comm);
int PMPI_Unpack(void*, int, int *, void *, int, MPI_Datatype, MPI_Comm);
int PMPI_Pack_size(int, MPI_Datatype, MPI_Comm, int *);
int PMPI_Barrier(MPI_Comm );
int PMPI_Bcast(void* buffer, int, MPI_Datatype, int, MPI_Comm );
int PMPI_Gather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, int, MPI_Comm); 
int PMPI_Gatherv(void* , int, MPI_Datatype, void*, int *, int *, MPI_Datatype, int, MPI_Comm); 
int PMPI_Scatter(void* , int, MPI_Datatype, void*, int, MPI_Datatype, int, MPI_Comm);
int PMPI_Scatterv(void* , int *, int *displs, MPI_Datatype, void*, int, MPI_Datatype, int, MPI_Comm);
int PMPI_Allgather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, MPI_Comm);
int PMPI_Allgatherv(void* , int, MPI_Datatype, void*, int *, int *, MPI_Datatype, MPI_Comm);
int PMPI_Alltoall(void* , int, MPI_Datatype, void*, int, MPI_Datatype, MPI_Comm);
int PMPI_Alltoallv(void* , int *, int *, MPI_Datatype, void*, int *, int *, MPI_Datatype, MPI_Comm);
int PMPI_Reduce(void* , void*, int, MPI_Datatype, MPI_Op, int, MPI_Comm);
int PMPI_Op_create(MPI_User_function *, int, MPI_Op *);
int PMPI_Op_free( MPI_Op *);
int PMPI_Allreduce(void* , void*, int, MPI_Datatype, MPI_Op, MPI_Comm);
int PMPI_Reduce_scatter(void* , void*, int *, MPI_Datatype, MPI_Op, MPI_Comm);
int PMPI_Scan(void* , void*, int, MPI_Datatype, MPI_Op, MPI_Comm );
int PMPI_Group_size(MPI_Group group, int *);
int PMPI_Group_rank(MPI_Group group, int *);
int PMPI_Group_translate_ranks (MPI_Group, int, int *, MPI_Group, int *);
int PMPI_Group_compare(MPI_Group, MPI_Group, int *);
int PMPI_Comm_group(MPI_Comm, MPI_Group *);
int PMPI_Group_union(MPI_Group, MPI_Group, MPI_Group *);
int PMPI_Group_intersection(MPI_Group, MPI_Group, MPI_Group *);
int PMPI_Group_difference(MPI_Group, MPI_Group, MPI_Group *);
int PMPI_Group_incl(MPI_Group group, int, int *, MPI_Group *);
int PMPI_Group_excl(MPI_Group group, int, int *, MPI_Group *);
int PMPI_Group_range_incl(MPI_Group group, int, int [][3], MPI_Group *);
int PMPI_Group_range_excl(MPI_Group group, int, int [][3], MPI_Group *);
int PMPI_Group_free(MPI_Group *);
int PMPI_Comm_size(MPI_Comm, int *);
int PMPI_Comm_rank(MPI_Comm, int *);
int PMPI_Comm_compare(MPI_Comm, MPI_Comm, int *);
int PMPI_Comm_dup(MPI_Comm, MPI_Comm *);
int PMPI_Comm_create(MPI_Comm, MPI_Group, MPI_Comm *);
int PMPI_Comm_split(MPI_Comm, int, int, MPI_Comm *);
int PMPI_Comm_free(MPI_Comm *);
int PMPI_Comm_test_inter(MPI_Comm, int *);
int PMPI_Comm_remote_size(MPI_Comm, int *);
int PMPI_Comm_remote_group(MPI_Comm, MPI_Group *);
int PMPI_Intercomm_create(MPI_Comm, int, MPI_Comm, int, int, MPI_Comm *);
int PMPI_Intercomm_merge(MPI_Comm, int, MPI_Comm *);
int PMPI_Keyval_create(MPI_Copy_function *, MPI_Delete_function *, int *, void*);
int PMPI_Keyval_free(int *);
int PMPI_Attr_put(MPI_Comm, int, void*);
int PMPI_Attr_get(MPI_Comm, int, void *, int *);
int PMPI_Attr_delete(MPI_Comm, int);
int PMPI_Topo_test(MPI_Comm, int *);
int PMPI_Cart_create(MPI_Comm, int, int *, int *, int, MPI_Comm *);
int PMPI_Dims_create(int, int, int *);
int PMPI_Graph_create(MPI_Comm, int, int *, int *, int, MPI_Comm *);
int PMPI_Graphdims_get(MPI_Comm, int *, int *);
int PMPI_Graph_get(MPI_Comm, int, int, int *, int *);
int PMPI_Cartdim_get(MPI_Comm, int *);
int PMPI_Cart_get(MPI_Comm, int, int *, int *, int *);
int PMPI_Cart_rank(MPI_Comm, int *, int *);
int PMPI_Cart_coords(MPI_Comm, int, int, int *);
int PMPI_Graph_neighbors_count(MPI_Comm, int, int *);
int PMPI_Graph_neighbors(MPI_Comm, int, int, int *);
int PMPI_Cart_shift(MPI_Comm, int, int, int *, int *);
int PMPI_Cart_sub(MPI_Comm, int *, MPI_Comm *);
int PMPI_Cart_map(MPI_Comm, int, int *, int *, int *);
int PMPI_Graph_map(MPI_Comm, int, int *, int *, int *);
int PMPI_Get_processor_name(char *, int *);
int PMPI_Get_version(int *, int *);
int PMPI_Errhandler_create(MPI_Handler_function *, MPI_Errhandler *);
int PMPI_Errhandler_set(MPI_Comm, MPI_Errhandler);
int PMPI_Errhandler_get(MPI_Comm, MPI_Errhandler *);
int PMPI_Errhandler_free(MPI_Errhandler *);
int PMPI_Error_string(int, char *, int *);
int PMPI_Error_class(int, int *);

int PMPI_Type_get_envelope(MPI_Datatype, int *, int *, int *, int *); 
int PMPI_Type_get_contents(MPI_Datatype, int, int, int, int *, MPI_Aint *, MPI_Datatype *);
int PMPI_Type_create_subarray(int, int *, int *, int *, int, MPI_Datatype, MPI_Datatype *);
int PMPI_Type_create_darray(int, int, int, int *, int *, int *, int *, int, MPI_Datatype, MPI_Datatype *);
int PMPI_Info_create(MPI_Info *);
int PMPI_Info_set(MPI_Info, char *, char *);
int PMPI_Info_delete(MPI_Info, char *);
int PMPI_Info_get(MPI_Info, char *, int, char *, int *);
int PMPI_Info_get_valuelen(MPI_Info, char *, int *, int *);
int PMPI_Info_get_nkeys(MPI_Info, int *);
int PMPI_Info_get_nthkey(MPI_Info, int, char *);
int PMPI_Info_dup(MPI_Info, MPI_Info *);
int PMPI_Info_free(MPI_Info *);

MPI_Fint PMPI_Info_c2f(MPI_Info);
MPI_Info PMPI_Info_f2c(MPI_Fint);

/* Wtime done above */
int PMPI_Init(int *, char ***);
int PMPI_Init_thread( int *, char ***, int, int * );
int PMPI_Finalize(void);
int PMPI_Initialized(int *);
int PMPI_Abort(MPI_Comm, int);
/* MPI-2 communicator naming functions */
int PMPI_Comm_set_name(MPI_Comm, char *);
int PMPI_Comm_get_name(MPI_Comm, char *, int *); 
#ifdef HAVE_NO_C_CONST
/* Default Solaris compiler does not accept const but does accept prototypes */
#if defined(USE_STDARG) 
int PMPI_Pcontrol(int, ...);
#else
int PMPI_Pcontrol(int);
#endif
#else
int PMPI_Pcontrol(const int, ...);
#endif

/* external */
int PMPI_Status_set_cancelled( MPI_Status *, int );
int PMPI_Status_set_elements( MPI_Status *, MPI_Datatype, int );

#endif  /* MPI_BUILD_PROFILING */
/* End of MPI bindings */

#if defined(__cplusplus)
}
/* Add the C++ bindings */
/* 
   If MPICH_SKIP_MPICXX is defined, the mpi++.h file will *not* be included.
   This is necessary, for example, when building the C++ interfaces.  It
   can also be used when you want to use a C++ compiler to compile C code,
   and do not want to load the C++ bindings
 */
#if defined(HAVE_MPI_CPP) && !defined(MPICH_SKIP_MPICXX)
#include "mpi++.h"
#endif 
#endif

/* This is a special macro to let the MPI library check that a program 
   was compiled with the same header file as an application.  Define 
   STRICT_MPI
   to keep MPI_Init from being redefined as a macro.

   Unfortunately, this interfers with tools that make use of the
   MPI profiling interface and define an implementation of MPI_Init.
 */
#ifdef FOO
#ifndef STRICT_MPI
#define MPI_Init( a, b ) MPI_Init_vcheck( a, b, MPICH_VERSION )
#define PMPI_Init( a, b ) PMPI_Init_vcheck( a, b, MPICH_VERSION )
#endif
#endif

#endif


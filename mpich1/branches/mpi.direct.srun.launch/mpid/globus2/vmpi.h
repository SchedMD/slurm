#if !defined(MPICH_GLOBUS2_VMPI_H)
#define MPICH_GLOBUS2_VMPI_H 1

#include "chconfig.h"

#if defined(VMPI)

extern int				VMPI_MyWorldSize;
extern int				VMPI_MyWorldRank;
extern int *				VMPI_VGRank_to_GRank;
extern int *				VMPI_GRank_to_VGRank;

/*
 * Define constants used as intermediaries when translating between MPICH and
 * the vendor's MPI implementation
 */
#define VMPI_ANY_TAG (-1)
#define VMPI_ANY_SOURCE (-2)
#define VMPI_UNDEFINED (-32766)

#define VMPI_CHAR		0
#define VMPI_CHARACTER		1
#define VMPI_UNSIGNED_CHAR      2
#define VMPI_BYTE		3
#define VMPI_SHORT		4
#define VMPI_UNSIGNED_SHORT     5
#define VMPI_INT		6
#define VMPI_UNSIGNED		7
#define VMPI_LONG		8
#define VMPI_UNSIGNED_LONG      9
#define VMPI_FLOAT		10
#define VMPI_DOUBLE		11
#define VMPI_LONG_DOUBLE	12
#define VMPI_LONG_LONG_INT      13
#define VMPI_LONG_LONG		14
#define VMPI_PACKED		15
#define VMPI_LB			16
#define VMPI_UB			17
#define VMPI_FLOAT_INT		18
#define VMPI_DOUBLE_INT		19
#define VMPI_LONG_INT		20
#define VMPI_SHORT_INT		21
#define VMPI_2INT		22
#define VMPI_LONG_DOUBLE_INT    23
#define VMPI_COMPLEX		24
#define VMPI_DOUBLE_COMPLEX     25
#define VMPI_LOGICAL		26
#define VMPI_REAL		27
#define VMPI_DOUBLE_PRECISION   28
#define VMPI_INTEGER		29
#define VMPI_2INTEGER		30
#define VMPI_2COMPLEX		31
#define VMPI_2DOUBLE_COMPLEX    32
#define VMPI_2REAL		33
#define VMPI_2DOUBLE_PRECISION  34

#define VMPI_SUCCESS          0      /* Successful return code */
#define VMPI_ERR_BUFFER       1      /* Invalid buffer pointer */
#define VMPI_ERR_COUNT        2      /* Invalid count argument */
#define VMPI_ERR_TYPE         3      /* Invalid datatype argument */
#define VMPI_ERR_TAG          4      /* Invalid tag argument */
#define VMPI_ERR_COMM         5      /* Invalid communicator */
#define VMPI_ERR_RANK         6      /* Invalid rank */
#define VMPI_ERR_ROOT         7      /* Invalid root */
#define VMPI_ERR_GROUP        8      /* Invalid group */
#define VMPI_ERR_OP           9      /* Invalid operation */
#define VMPI_ERR_TOPOLOGY    10      /* Invalid topology */
#define VMPI_ERR_DIMS        11      /* Invalid dimension argument */
#define VMPI_ERR_ARG         12      /* Invalid argument */
#define VMPI_ERR_UNKNOWN     13      /* Unknown error */
#define VMPI_ERR_TRUNCATE    14      /* Message truncated on receive */
#define VMPI_ERR_OTHER       15      /* Other error; use Error_string */
#define VMPI_ERR_INTERN      16      /* Internal error code    */
#define VMPI_ERR_IN_STATUS   17      /* Look in status for error value */
#define VMPI_ERR_PENDING     18      /* Pending request */
#define VMPI_ERR_REQUEST     19      /* Invalid mpi_request handle */
#define VMPI_ERR_ACCESS      20      /* */
#define VMPI_ERR_AMODE       21      /* */
#define VMPI_ERR_BAD_FILE    22      /* */
#define VMPI_ERR_CONVERSION  23      /* */
#define VMPI_ERR_DUP_DATAREP 24      /* */
#define VMPI_ERR_FILE_EXISTS 25      /* */
#define VMPI_ERR_FILE_IN_USE 26      /* */
#define VMPI_ERR_FILE        27      /* */
#define VMPI_ERR_INFO        28      /* */
#define VMPI_ERR_INFO_KEY    29      /* */
#define VMPI_ERR_INFO_VALUE  30      /* */
#define VMPI_ERR_INFO_NOKEY  31      /* */
#define VMPI_ERR_IO          32      /* */
#define VMPI_ERR_NAME        33      /* */
#define VMPI_ERR_EXHAUSTED   34      /* */
#define VMPI_ERR_NOT_SAME    35      /* */
#define VMPI_ERR_NO_SPACE    36      /* */
#define VMPI_ERR_NO_SUCH_FILE 37     /* */
#define VMPI_ERR_PORT        38      /* */
#define VMPI_ERR_QUOTA       39      /* */
#define VMPI_ERR_READ_ONLY   40      /* */
#define VMPI_ERR_SERVICE     41      /* */
#define VMPI_ERR_SPAWN       42      /* */
#define VMPI_ERR_UNSUPPORTED_DATAREP   43  /* */
#define VMPI_ERR_UNSUPPORTED_OPERATION 44 /* */
#define VMPI_ERR_WIN         45      /* */
#define VMPI_ERR_LASTCODE    0x3FFFFFFF      /* Last error code*/

/* 
 * we needed to add this additional level of indirection
 * because the Cray's vMPI already defines some of these mp_xxx symbols
 */

#define mp_cancel mpich_globus2_mp_cancel
#define mp_comm_dup mpich_globus2_mp_comm_dup
#define mp_comm_free mpich_globus2_mp_comm_free
#define mp_comm_get_size mpich_globus2_mp_comm_get_size
#define mp_comm_split mpich_globus2_mp_comm_split
#define mp_create_miproto mpich_globus2_mp_create_miproto
#define mp_finalize mpich_globus2_mp_finalize
#define mp_get_count mpich_globus2_mp_get_count
#define mp_get_elements mpich_globus2_mp_get_elements
#define mp_init mpich_globus2_mp_init
#define mp_intercomm_create mpich_globus2_mp_intercomm_create
#define mp_intercomm_merge mpich_globus2_mp_intercomm_merge
#define mp_iprobe mpich_globus2_mp_iprobe
#define mp_isend mpich_globus2_mp_isend
#define mp_issend mpich_globus2_mp_issend
#define mp_miproto mpich_globus2_mp_miproto
#define mp_probe mpich_globus2_mp_probe
#define mp_recv mpich_globus2_mp_recv
#define mp_request_free mpich_globus2_mp_request_free
#define mp_send mpich_globus2_mp_send
#define mp_ssend mpich_globus2_mp_ssend
#define mp_status_get_error mpich_globus2_mp_status_get_error
#define mp_status_get_source mpich_globus2_mp_status_get_source
#define mp_status_get_tag mpich_globus2_mp_status_get_tag
#define mp_test mpich_globus2_mp_test
#define mp_test_cancelled mpich_globus2_mp_test_cancelled
#define mp_type_commit mpich_globus2_mp_type_commit
#define mp_type_contiguous mpich_globus2_mp_type_contiguous
#define mp_type_free mpich_globus2_mp_type_free
#define mp_type_hindexed mpich_globus2_mp_type_hindexed
#define mp_type_hvector mpich_globus2_mp_type_hvector
#define mp_type_permanent_free mpich_globus2_mp_type_permanent_free
#define mp_type_permanent_setup mpich_globus2_mp_type_permanent_setup
#define mp_type_struct mpich_globus2_mp_type_struct
#define mp_type_vector mpich_globus2_mp_type_vector
#define mp_wait mpich_globus2_mp_wait
/* START Special boostrap wrappers for vMPI functions */
#define mp_bootstrap_bcast mpich_globus2_mp_bootstrap_bcast
#define mp_bootstrap_gather mpich_globus2_mp_bootstrap_gather
#define mp_bootstrap_gatherv mpich_globus2_mp_bootstrap_gatherv
/* END Special boostrap wrappers for vMPI functions */

int mp_init(
    int *				argc,
    char ***				argv);

void mp_finalize();

void mp_create_miproto(
    char **				mp_miproto,
    int *				nbytes);

int mp_send(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm);

int mp_isend(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm,
    void *				request);

int mp_ssend(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm);

int mp_issend(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm,
    void *				request);

int mp_cancel(void *request);

int mp_recv(
    void *				buff, 
    int					count, 
    void *				type, 
    int					src, 
    int					tag,
    void *				comm,
    void *				status);

int mp_wait(
    void *                              request,
    void *                              status);

int mp_test_cancelled(
    void *                              status,
    int *                               flag);

int mp_test(
    void *				request,
    int *				flag,
    void *				status);

int mp_probe(
    int					src,
    int					tag,
    void *				comm,
    void *				status);

int mp_iprobe(
    int					src,
    int					tag,
    void *				comm,
    int *				flag,
    void *				status);

int mp_get_count(
    void *				status,
    void *				type, 
    int *				count);

int mp_get_elements(
    void *				status,
    void *				type, 
    int *				elements);

int mp_status_get_source(
    void *				status);

int mp_status_get_tag(
    void *				status);

int mp_status_get_error(
    void *				status);

int mp_comm_get_size();

int mp_comm_split(
    void *				oldcomm,
    int					color,
    int					key,
    void *				newcomm);

int mp_comm_dup(
    void *				oldcomm,
    void *				newcomm);

int mp_intercomm_create(
    void *				local_comm,
    int					local_leader,
    void *				peer_comm,
    int					remote_leader,
    int					tag,
    void *				newintercomm);

int mp_intercomm_merge(
    void *				intercomm,
    int					high,
    void *				intracomm);


int mp_comm_free(
    void *				comm);

int mp_request_free(
    void *				request);

/********************************/
/* Derived Datatype Contructors */
/********************************/

int mp_type_commit(
    void *				type);

int mp_type_free(
    void *				type);

int mp_type_permanent_setup(
    void *				mpi_type,
    int					vmpi_type);

int mp_type_permanent_free(
    void *				mpi_type,
    int					vmpi_type);

int mp_type_contiguous(
    int					count,
    void *				old_type,
    void *				new_type);

int mp_type_hvector(
    int					count, 
    int					blocklength, 
    MPI_Aint				stride, 
    void *				old_type, 
    void *				new_type);

int mp_type_hindexed(
    int					count, 
    int *				blocklengths, 
    MPI_Aint *				displacements, 
    void *				old_type, 
    void *				new_type);

int mp_type_struct(
    int					count, 
    int *				blocklengths, 
    MPI_Aint *				displacements, 
    void *				old_types, 
    void *				new_type);

/******************************************************/
/* START Special boostrap wrappers for vMPI functions */
/******************************************************/

int mp_bootstrap_bcast(void *buff, int  count, int type);
int mp_bootstrap_gather(void *sbuff, int scnt, void *rbuff, int rcnt);
int mp_bootstrap_gatherv(void *sendbuff,
                        int sendcount,
                        void *recvbuff,
                        int *recvcounts,
                        int *displs);

/****************************************************/
/* END Special boostrap wrappers for vMPI functions */
/****************************************************/

#if !defined(VMPI_NO_MPICH)

int vmpi_error_to_mpich_error(
    int					vmpi_error);

int vmpi_grank_to_mpich_grank(
    int					vmpi_grank);

#endif /* !defined(VMPI_NO_MPICH) */

#endif /* defined(VMPI) */
#endif /* !defined(MPICH_GLOBUS2_VMPI_H) */

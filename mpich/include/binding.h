#ifndef __MPI_BINDINGS
#define __MPI_BINDINGS

#include "mpi.h"

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
int MPI_Iprobe(int, int, MPI_Comm, int *, MPI_Status *);
int MPI_Probe(int, int, MPI_Comm, MPI_Status *);
int MPI_Cancel(MPI_Request *);
int MPI_Test_cancelled(MPI_Status *, int *);
int MPI_Send_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Bsend_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Ssend_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Rsend_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Recv_init(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Start(MPI_Request *);
int MPI_Startall(int, MPI_Request *);
int MPI_Sendrecv(void *, int, MPI_Datatype,int, int, void *, int, 
		 MPI_Datatype, int, int, MPI_Comm, MPI_Status *);
int MPI_Sendrecv_replace(void*, int, MPI_Datatype, 
			 int, int, int, int, MPI_Comm, MPI_Status *);
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
int MPI_Type_count(MPI_Datatype, int *);
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
int MPI_Gather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
	       int, MPI_Comm); 
int MPI_Gatherv(void* , int, MPI_Datatype, void*, int *, int *, 
		MPI_Datatype, int, MPI_Comm); 
int MPI_Scatter(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
		int, MPI_Comm);
int MPI_Scatterv(void* , int *, int *,  MPI_Datatype, void*, int, 
		 MPI_Datatype, int, MPI_Comm);
int MPI_Allgather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
		  MPI_Comm);
int MPI_Allgatherv(void* , int, MPI_Datatype, void*, int *, int *, 
		   MPI_Datatype, MPI_Comm);
int MPI_Alltoall(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
		 MPI_Comm);
int MPI_Alltoallv(void* , int *, int *, MPI_Datatype, void*, int *, 
		  int *, MPI_Datatype, MPI_Comm);
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
int MPI_Keyval_create(MPI_Copy_function *, MPI_Delete_function *, 
		      int *, void*);
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
int MPI_Errhandler_create(MPI_Handler_function *, 
			  MPI_Errhandler *);
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
#ifndef MPI_Init
/* Don't define if this is a macro used to define MPI_Init with a 
   third, version, argument */
int MPI_Init(int *, char ***);
#endif
int MPI_Init_thread(int *, char ***, int, int *);
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
int MPI_Type_create_indexed_block(int, int, int *, MPI_Datatype, 
				  MPI_Datatype *);
int MPI_Type_get_envelope(MPI_Datatype, int *, int *, int *, int *); 
int MPI_Type_get_contents(MPI_Datatype, int, int, int, int *, 
             MPI_Aint *, MPI_Datatype *);
int MPI_Type_create_subarray(int, int *, int *, int *, int, 
                      MPI_Datatype, MPI_Datatype *);

int MPI_Type_create_darray(int, int, int, int *, int *, int *, int *, 
                    int, MPI_Datatype, MPI_Datatype *);

int MPI_Info_create(MPI_Info *);
int MPI_Info_set(MPI_Info, char *, char *);
int MPI_Info_delete(MPI_Info, char *);
int MPI_Info_get(MPI_Info, char *, int, char *, int *);
int MPI_Info_get_valuelen(MPI_Info, char *, int *, int *);
int MPI_Info_get_nkeys(MPI_Info, int *);
int MPI_Info_get_nthkey(MPI_Info, int, char *);
int MPI_Info_dup(MPI_Info, MPI_Info *);
int MPI_Info_free(MPI_Info *);

MPI_Fint MPI_Info_c2f(MPI_Info);
MPI_Info MPI_Info_f2c(MPI_Fint);

MPI_Fint MPI_Request_c2f( MPI_Request );

/* external */
int MPI_Status_set_cancelled( MPI_Status *, int );
int MPI_Status_set_elements( MPI_Status *, MPI_Datatype, int );



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
int PMPI_Testall(int, MPI_Request *, int *, MPI_Status *);
int PMPI_Waitsome(int, MPI_Request *, int *, int *, MPI_Status *);
int PMPI_Testsome(int, MPI_Request *, int *, int *, MPI_Status *);
int PMPI_Iprobe(int, int, MPI_Comm, int *, MPI_Status *);
int PMPI_Probe(int, int, MPI_Comm, MPI_Status *);
int PMPI_Cancel(MPI_Request *);
int PMPI_Test_cancelled(MPI_Status *, int *);
int PMPI_Send_init(void*, int, MPI_Datatype, int, 
		  int, MPI_Comm, MPI_Request *);
int PMPI_Bsend_init(void*, int, MPI_Datatype, int, 
		   int, MPI_Comm, MPI_Request *);
int PMPI_Ssend_init(void*, int, MPI_Datatype, int, 
		   int, MPI_Comm, MPI_Request *);
int PMPI_Rsend_init(void*, int, MPI_Datatype, int, 
		   int, MPI_Comm, MPI_Request *);
int PMPI_Recv_init(void*, int, MPI_Datatype, int, 
		  int, MPI_Comm, MPI_Request *);
int PMPI_Start(MPI_Request *);
int PMPI_Startall(int, MPI_Request *);
int PMPI_Sendrecv(void *, int, MPI_Datatype, 
		 int, int, void *, int, 
		 MPI_Datatype, int, int, 
		 MPI_Comm, MPI_Status *);
int PMPI_Sendrecv_replace(void*, int, MPI_Datatype, 
			 int, int, int, int, 
			 MPI_Comm, MPI_Status *);
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
int PMPI_Type_count(MPI_Datatype, int *);
int PMPI_Type_lb(MPI_Datatype, MPI_Aint*);
int PMPI_Type_ub(MPI_Datatype, MPI_Aint*);
int PMPI_Type_commit(MPI_Datatype *);
int PMPI_Type_free(MPI_Datatype *);
int PMPI_Get_elements(MPI_Status *, MPI_Datatype, int *);
int PMPI_Pack(void*, int, MPI_Datatype, void *, int, int *,  MPI_Comm);
int PMPI_Unpack(void*, int, int *, void *, int, MPI_Datatype, MPI_Comm);
int PMPI_Pack_size(int, MPI_Datatype, MPI_Comm, int *);
int PMPI_Barrier(MPI_Comm );
int PMPI_Bcast(void*, int, MPI_Datatype, int, MPI_Comm );
int PMPI_Gather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
	       int, MPI_Comm); 
int PMPI_Gatherv(void* , int, MPI_Datatype, void*, int *, int *, 
		MPI_Datatype, int, MPI_Comm); 
int PMPI_Scatter(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
		int, MPI_Comm);
int PMPI_Scatterv(void* , int *, int *, MPI_Datatype, void*, int, 
		 MPI_Datatype, int, MPI_Comm);
int PMPI_Allgather(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
		  MPI_Comm);
int PMPI_Allgatherv(void* , int, MPI_Datatype, void*, int *, int *, 
		   MPI_Datatype, MPI_Comm);
int PMPI_Alltoall(void* , int, MPI_Datatype, void*, int, MPI_Datatype, 
		 MPI_Comm);
int PMPI_Alltoallv(void* , int *, int *, MPI_Datatype, void*, int *, 
		  int *, MPI_Datatype, MPI_Comm);
int PMPI_Reduce(void* , void*, int, MPI_Datatype, MPI_Op, int, MPI_Comm);
int PMPI_Op_create(MPI_User_function *, int, MPI_Op *);
int PMPI_Op_free( MPI_Op *);
int PMPI_Allreduce(void* , void*, int, MPI_Datatype, MPI_Op, MPI_Comm);
int PMPI_Reduce_scatter(void* , void*, int *, MPI_Datatype, MPI_Op, MPI_Comm);
int PMPI_Scan(void* , void*, int, MPI_Datatype, MPI_Op, MPI_Comm );
int PMPI_Group_size(MPI_Group group, int *);
int PMPI_Group_rank(MPI_Group group, int *rank);
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
int PMPI_Comm_compare(MPI_Comm comm1, MPI_Comm comm2, int *result);
int PMPI_Comm_dup(MPI_Comm, MPI_Comm *);
int PMPI_Comm_create(MPI_Comm, MPI_Group group, MPI_Comm *);
int PMPI_Comm_split(MPI_Comm, int color, int key, MPI_Comm *);
int PMPI_Comm_free(MPI_Comm *comm);
int PMPI_Comm_test_inter(MPI_Comm, int *);
int PMPI_Comm_remote_size(MPI_Comm, int *);
int PMPI_Comm_remote_group(MPI_Comm, MPI_Group *);
int PMPI_Intercomm_create(MPI_Comm local_comm, int, MPI_Comm peer_comm, int, 
			 int, MPI_Comm *);
int PMPI_Intercomm_merge(MPI_Comm intercomm, int, MPI_Comm *);
int PMPI_Keyval_create(MPI_Copy_function *, MPI_Delete_function *, 
		      int *, void*);
int PMPI_Keyval_free(int *);
int PMPI_Attr_put(MPI_Comm, int, void *);
int PMPI_Attr_get(MPI_Comm, int, void *, int *);
int PMPI_Attr_delete(MPI_Comm, int);
int PMPI_Topo_test(MPI_Comm, int *);
int PMPI_Cart_create(MPI_Comm comm_old, int, int *, int *, int, MPI_Comm *);
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
int PMPI_Type_get_contents(MPI_Datatype, int, int, int, int *, 
             MPI_Aint *, MPI_Datatype *);
int PMPI_Type_create_subarray(int, int *, int *, int *, int, 
                      MPI_Datatype, MPI_Datatype *);
int PMPI_Type_create_darray(int, int, int, int *, int *, int *, int *, 
                    int, MPI_Datatype, MPI_Datatype *);
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
#ifndef PMPI_Init
/* See MPI_Init above */
int PMPI_Init(int *, char ***);
#endif
int PMPI_Init_thread(int *, char ***, int, int *);
int PMPI_Finalize(void);
int PMPI_Initialized(int *);
int PMPI_Abort(MPI_Comm, int);
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


#endif

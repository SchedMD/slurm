#ifndef CHCONFIG
#define CHCONFIG
/* Special configuration information goes here */

#define MPID_CommInit(oldcomm,newcomm) MPI_SUCCESS
#define MPID_CommFree(comm)            MPI_SUCCESS

/* VERY limited buffering in MPL.  VERY close to only providing Ssend */
#ifndef MPID_PKT_MAX_DATA_SIZE
#define MPID_PKT_MAX_DATA_SIZE 64
#endif
#ifndef MPID_TINY_BUFFERS
#define MPID_TINY_BUFFERS
#endif
#ifndef MPID_LIMITED_BUFFERS
#define MPID_LIMITED_BUFFERS
#endif
#endif

/* Used for packet control */
#undef MPID_USE_SHMEM

/* Used for making sure we get the last packet from a tcp connection */
#undef MPID_GET_LAST_PKT

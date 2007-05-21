#ifndef CHCONFIG
#define CHCONFIG
/* Special configuration information goes here */

/* See ch_p4 for definitions for heterogeneity */
#define MPID_CommInit(oldcomm,newcomm) MPI_SUCCESS
#define MPID_CommFree(comm)            MPI_SUCCESS

/* See ch_mpl for definitions for limited buffering */
#endif

/* mpe_counter.c */
/* Fortran interface file for sun4 */
#ifndef DEBUG_ALL
#define DEBUG_ALL
#endif
#include <stdio.h>
#include "mpeconf.h"
#include "mpe.h"
 int  mpe_counter_create_( oldcomm, smaller_comm, counter_comm)
MPI_Comm  *oldcomm,  *smaller_comm,  *counter_comm;
{
return MPE_Counter_create(*oldcomm,
	(MPI_Comm* )*((int*)smaller_comm),
	(MPI_Comm* )*((int*)counter_comm));
}
 int  mpe_counter_free_(counter_comm, smaller_comm)
MPI_Comm *counter_comm;
MPI_Comm *smaller_comm;
{
return MPE_Counter_free(*counter_comm,*smaller_comm);
}
 int  mpe_counter_nxtval_(counter_comm, value)
MPI_Comm *counter_comm;
int *value;
{
return MPE_Counter_nxtval(*counter_comm,value);
}

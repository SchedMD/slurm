#include "mpeconf.h"
#include "mpi.h"
#include "mpeexten.h"

/*@
  MPE_Comm_global_rank - Returns the rank in MPI_COMM_WORLD for a
  given (communicator,rank) pair

  Input Parameters:
+ comm - Communicator
- rank - Rank in comm

  Output Parameters:
. grank - Rank in comm world
@*/
void MPE_Comm_global_rank( comm, rank, grank )
MPI_Comm comm;
int      rank, *grank;
{
/* We could cache world_group, at least, but then we'd have no way to
   free it later */
    MPI_Group group, world_group;
    int lrank = rank;/* Just in case rank has no address (passed in register) */
 
    MPI_Comm_group( comm, &group );
    MPI_Comm_group( MPI_COMM_WORLD, &world_group );
    MPI_Group_translate_ranks( group, 1, &lrank, world_group, grank );
    MPI_Group_free( &group );
    MPI_Group_free( &world_group );
}

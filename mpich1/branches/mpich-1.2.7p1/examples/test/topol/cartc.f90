program topology
  
     implicit none
     include "mpif.h"
     integer, parameter :: Ndim=2
     integer :: Rang, Nprocs, Comm, info
     integer, dimension(Ndim) :: Dims
     logical, dimension(Ndim) :: Period
     logical                  :: Reorder=.FALSE.

     Period(:) = .FALSE.
     CALL MPI_INIT(info)
     CALL MPI_COMM_RANK( MPI_COMM_WORLD, rang, info )
     CALL MPI_COMM_SIZE( MPI_COMM_WORLD, Nprocs, info )
     Dims(:) = 0
     CALL MPI_DIMS_CREATE( Nprocs, Ndim, Dims, info )
     CALL MPI_CART_CREATE( MPI_COMM_WORLD, Ndim, Dims, Period, Reorder, &
                           Comm, info )
     print *, "Rang : ",rang," New Comm cart : ",Comm
     call MPI_FINALIZE(info)
end program topology

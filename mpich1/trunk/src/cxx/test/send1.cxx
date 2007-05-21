#include "mpi.h"
#include "mpicxxbase.h"
#include <iostream.h>

using namespace MPI;
int main( int argc, char *argv[] )
{
  int size, rank, i;
  Request request;
  Status  status;
  int     buf[10];

  MPI::Init( argc, argv );

  rank = MPI::COMM_WORLD.Get_rank();
  size = MPI::COMM_WORLD.Get_size();

  if (size < 2) {
    cout << "Size must be at least 2" << endl;
    COMM_WORLD.Abort( 1 );
  }

  if (rank == 0) {
    for (i=0; i<10; i++) buf[i] = i + 1;
    request = COMM_WORLD.Isend( buf, 10, INT, 1, 13 );
    request.Wait( );
  }
  else if (rank == 1) {
    for (i=0; i<10; i++) buf[i] = -10;
    COMM_WORLD.Recv( buf, 10, INT, 0, 13, status );
    for (i=0; i<10; i++) {
      if (buf[i] != i + 1) {
	cout << "buf[" << i << "] = " << buf[i] << endl;
      }
    }
  }

  MPI::Finalize();
  return 0;
}

#include "mpi.h"
#include "mpicxxbase.h"
#include <iostream.h>

int main( int argc, char *argv[] )
{
  MPI::Init( argc, argv );
  cout << "size= " << MPI::COMM_WORLD.Get_size() << "\n";
  cout << "myrank = " << MPI::COMM_WORLD.Get_rank() << "\n";
  MPI::Finalize();
  return 0;
}

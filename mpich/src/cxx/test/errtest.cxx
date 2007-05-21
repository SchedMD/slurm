#include "mpi.h"
#include "mpicxxbase.h"
#include <iostream.h>

// This test trys the error handling

int main( int argc, char *argv[] )
{
  MPI::Init( argc, argv );

  cout << "size= " << MPI::COMM_WORLD.Get_size() << "\n";
  cout << "myrank = " << MPI::COMM_WORLD.Get_rank() << "\n";
  
  MPI::COMM_WORLD.Set_errhandler( MPI::ERRORS_THROW_EXCEPTIONS );

  try {
    size = MPI::COMM_NULL.Get_size();
  }
  catch (MPI::Exception e) {
    cout << "Caught exception in get size with code " << e.Get_error_code() 
	 << " and message " << e.Get_error_string() << "\n";
    delete e;
  }

  MPI::Finalize();
  return 0;
}

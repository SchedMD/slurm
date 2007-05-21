// -*- c++ -*-
//
// Copyright 1997-2000, University of Notre Dame.
// Authors: Jeremy G. Siek, Jeffery M. Squyres, Michael P. McNally, and
//          Andrew Lumsdaine
// 
// This file is part of the Notre Dame C++ bindings for MPI.
// 
// You should have received a copy of the License Agreement for the Notre
// Dame C++ bindings for MPI along with the software; see the file
// LICENSE.  If not, contact Office of Research, University of Notre
// Dame, Notre Dame, IN 46556.
// 
// Permission to modify the code and to distribute modified code is
// granted, provided the text of this NOTICE is retained, a notice that
// the code was modified is included with the above COPYRIGHT NOTICE and
// with the COPYRIGHT NOTICE in the LICENSE file, and that the LICENSE
// file is distributed with the modified code.
// 
// LICENSOR MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED.
// By way of example, but not limitation, Licensor MAKES NO
// REPRESENTATIONS OR WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY
// PARTICULAR PURPOSE OR THAT THE USE OF THE LICENSED SOFTWARE COMPONENTS
// OR DOCUMENTATION WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS, TRADEMARKS
// OR OTHER RIGHTS.
// 
// Additional copyrights may follow.
//

#include <iostream.h>
#include <mpi++.h>


void user_bcast(int buffer[], int count, MPI::Intracomm comm);

int 
main(int argc, char **argv)
{
  int rank, size;
  int msg[10];

  // Start up MPI 
  MPI::Init(argc, argv);

  rank = MPI::COMM_WORLD.Get_rank();
  size = MPI::COMM_WORLD.Get_size();
 
  // Create an array to broadcast on the last process 
  if (rank == size - 1) {
    cout << "Broadcast: ";
    for (int i = 0; i < 10; i++) {
      msg[i] = i;
      cout << " " << msg[i] << " ";
    }
    cout << endl;
  }

  // Do the broadcast 
  user_bcast(msg, 10, MPI::COMM_WORLD);

  // If we are the console, print what we got 
  if (rank == 0) {
    cout << "process  " << rank << " got:";
    for (int i = 0; i < 10; i++)
      cout << " " << msg[i] << " ";
    cout << endl;
  }

  // Quit MPI 
  MPI::Finalize();

  return 0;
}


void
user_bcast(int buffer[], int count, MPI::Intracomm comm)
{
  int rank = comm.Get_rank();
  int size = comm.Get_size();

  /* The last process loops to send to everyone in the communicator. */
  /* Do a Waitall to wait for all messages to finish sending */

  if (rank == size - 1 ) {

    MPI::Request* reqarray = new MPI::Request[size-1];
    
    for (int dest = 0; dest < size - 1; dest++ )
      reqarray[dest] = comm.Isend(buffer, count, MPI::INT, dest, 4);

    MPI::Request::Waitall(size-1, reqarray);
  } 

  /* If we are not the last process process, we should go into a receive */
  /* and wait for it to complete */

  else {
    MPI::Request request;
    
    request = comm.Irecv(buffer, count, MPI::INT, size - 1, MPI::ANY_TAG);
    request.Wait();
  }
}

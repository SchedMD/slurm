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

#include "mpi2c++_test.h"
extern "C" {
#include <stdio.h>
#if !MPI2CPP_AIX
#include <unistd.h>
#endif
}
#include <iostream.h>
// WDG - including string.h here causes problems because mpi2c++_test.h 
// includes string.h within extern "C".  Do we really need to include
// it here?
#include <string.h>
#include "mpi++.h"


//
// Global vars for below
//

static int indent_level = 0;
static int column = 0;
static int num_bullets = -1;
static int waiting = 0;

static const char bullets[] = { '*', '-', 'o', '.', 0 };
static const int dest_column = 50;


//
// Local functions
//

static void check_for_failures(int my_code, const char *msg = 0);
static void Endline(const char *msg);
static inline void decrement(void) 
{ indent_level = (indent_level <= 0) ? 0 : indent_level - 1; };
static inline void increment(void)
{ indent_level++; };


//
// Testing
//
void
Testing(const char *message)
{
  int i;

  MPI::COMM_WORLD.Barrier();

  if (my_rank != 0)
    return;

  if (num_bullets == -1)
    for (num_bullets= 0; bullets[num_bullets] != 0; num_bullets++)
      continue;

  // Check to see if we are waiting at the end of a line already

  if (waiting)
    cout << endl;

  // Output the indenting and bullet

  column= 0;
  for (i= 0; i < indent_level; i++) {
    cout <<"  ";
    column += 2;
  }

  // Output the message 

  Push(message);
  if (indent_level == 0)
    cout << endl;
  cout << bullets[indent_level % num_bullets] << " " << message << "... ";
  cout.flush();

  column += 6 + strlen(message);
  for (i= column ; i < dest_column; i++)
    cout << " ";
  cout.flush();

  waiting= 1;
  increment();
}


//
// Pass
//
void
Pass(const char *msg)
{
  check_for_failures(0);

  decrement();
  Endline(msg);
}


//
// Sync
//
void
Sync(const char *msg)
{
  check_for_failures(0, msg);
}


//
// Postpone
//
void
Postpone(const char *class_name)
{
  static char buffer[1024];

  decrement();
  sprintf(buffer, "POSTPONED -- %s", class_name);
  Endline(buffer);
  cout.flush();
}


//
// Done
//
void
Done(const char *msg)
{
  decrement();
  Endline(msg);
  cout.flush();
}


//
// Fail
//
void
Fail(const char *msg)
{
  check_for_failures(1, msg);
}


//
// Abort
//
void
Abort(const char *msg)
{
  Endline("FAIL");

  cerr << endl;
  if (msg != 0)
    cerr << "MPI2C++ test suite: " << msg << endl;

  cerr << "MPI2C++ test suite: major error!" << endl;
  cerr << "MPI2C++ test suite: attempting to abort..." << endl;

  MPI::COMM_WORLD.Abort(-1);

  // Shouldn't reach here

  if (my_rank <= 0)
    cerr << "MPI2C++ test suite: terminated" << endl << endl;

  exit(-1);
}


//
// Endline
//
static void
Endline(const char *msg)
{
  if (my_rank != 0)
    return;

  cout.flush();
  if (waiting) {
    cout << msg << endl;
    Pop();
  }
  else {
    // Output the indenting and bullet
    
    int i;
    column= 0;
    for (i= 0; i < indent_level; i++) {
      cout << "  ";
      column += 2;
    }
    
    const char *line= Pop();
    int len= (line != 0) ? strlen(line) : 0;
    cout << bullets[indent_level % num_bullets] << " " << line << "... ";
    for (i= column + 6 + len; i < dest_column; i++)
      cout << " ";
    cout << msg << endl;
    cout.flush();
  }

  waiting= 0;
  column= 0;
}


//
// Check for failures among ranks
//
void
check_for_failures(int my_code, const char *msg)
{
  char emsg[150];
  static int num_fails;
  static int recv;
  static MPI::Status status;
  static int i;

  MPI::COMM_WORLD.Allreduce(&my_code, &num_fails, 1, MPI::INT, 
			    MPI::SUM);

  // Did someone fail? 

  if (num_fails == 0)
    return;

  // Yes, someone failed 

  if (my_rank > 0 && my_code == 1) {
    MPI::COMM_WORLD.Send(&my_rank, 1, MPI::INT, 0, 1234);
    MPI::COMM_WORLD.Send(msg, 150 * sizeof(char), MPI::CHAR, 0, 5678);
  }

  if (my_rank == 0) {
    // End the line with a FAIL, because someone failed
    
    Endline("FAIL");
    
    // If we have a descriptive message, print it
    
    cerr << endl;
    if (msg != 0)
      cerr << "MPI2C++ test suite: " << msg << endl;
    
    // Print right header message
    
    if (num_fails == 1)
      cerr << "MPI2C++ test suite: attempting to determine which rank failed..." 
	   << endl;
    else if (num_fails < comm_size)
      cerr << "MPI2C++ test suite: attempting to determine which " << num_fails << " ranks failed..." << endl;
    else
      cerr << "MPI2C++ test suite: all ranks failed" << endl;
    
    // Was I one of the failures (this could only be rank 0)?
    
    if (my_code == 1 && num_fails < comm_size)
      cerr << "MPI2C++ test suite: rank 0 failed" << endl;
    
    // Receive all the failure messages, print if num_fails < comm_size
    
    for (i = 0; i < num_fails - my_code; i++) {
      MPI::COMM_WORLD.Recv(&recv, 1, MPI::INT, MPI::ANY_SOURCE, 1234, status);
      if (num_fails < comm_size)
	cerr << "MPI2C++ test suite: rank " << recv << " failed" << endl;
      MPI::COMM_WORLD.Recv(&emsg, sizeof(emsg), MPI::CHAR, MPI::ANY_SOURCE, 5678, status);
      if (num_fails < comm_size) {
	cerr << "MPI2C++ test suite: ERROR MESSAGE FOLLOWS " << endl;
	cerr << emsg << endl;
      }
    }
    
    cerr << "MPI2C++ test suite: minor error" << endl;
    cerr << "MPI2C++ test suite: attempting to finalize..." << endl;
  }
  
  // We can only hope that this works...
  MPI::Finalize();
  
  if (my_rank == 0)
    cerr << "MPI2C++ test suite: terminated" << endl << endl;
  
  exit(0);

}

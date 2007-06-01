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

#include <signal.h>
#include <iostream.h>
#include "mpi2c++_test.h"


//
// Local variables
//

struct signal_map {
  int number;
  const char *name;
  struct sigaction *old_action;
};

struct signal_map block_me[] = {
  { SIGHUP, "Hangup", 0 },
  { SIGINT, "Interrupt",0  },
  { SIGQUIT, "Quit", 0 },
  { SIGTRAP, "IOT Trap", 0 },
  { SIGFPE, "Floating point exception", 0 },
  { SIGBUS, "Bus error", 0 },
  { SIGSEGV, "Segmentation fault", 0 },
  { SIGTERM, "Terminate", 0 },
  { SIGSTOP, "Stop", 0 },
  { SIGIOT, "IOT instruction", 0 },
  { SIGABRT, "Signal abort", 0 },
  { -1, (char *) -1, (struct sigaction *) -1 },
};


//
// Local functions
//

extern "C" {
#if MPI2CPP_BSD_SIGNAL
typedef void (*signal_handler)(...);
void handler(int sig, int code, struct sigcontext *scp, char *addr);
#elif MPI2CPP_SYSV_SIGNAL
typedef void (*signal_handler)(int);
void handler(int sig);
#endif
}


//
// init_signals
//
void
signal_init()
{
#if MPI2CPP_BSD_SIGNAL
  int i;
  struct sigaction n;

  n.sa_handler= (signal_handler) handler;
  n.sa_mask= 0;
  n.sa_flags= 0;

  for (i= 0; block_me[i].number >= 0; i++) {
    // some compilers are very picky...
    block_me[i].old_action= (struct sigaction *) new 
      char[sizeof(struct sigaction)];
    sigaction(block_me[i].number, &n, block_me[i].old_action);
  }
#elif MPI2CPP_SYSV_SIGNAL
  int i;

  for (i= 0; block_me[i].number >= 0; i++)
    signal(block_me[i].number, handler);
#endif
}


//
// handler
//
#if MPI2CPP_BSD_SIGNAL
void 
handler(int sig, int code, struct sigcontext *scp, char *addr)
#elif MPI2CPP_SYSV_SIGNAL
void 
handler(int sig)
#endif
{
  int i;

  // So that g++ -Wall won't complain

#if MPI2CPP_BSD_SIGNAL
  i= code;
  i= (int) scp;
  i= (int) addr;
#endif

  // Find the name

  for (i= 0; block_me[i].number >= 0; i++)
    if (block_me[i].number == sig)
      break;
  
  cout << endl << endl << "MPI2C++ test suite (rank " << my_rank << "): ";
  if (block_me[i].number == -1)
    cout << "Unknown signal (" << sig << ") caught";
  else
    cout << block_me[i].name;
  cout << endl << "MPI2C++ test suite: aborting..." << endl;
  cout.flush();
    
  // Try to abort
  
  MPI::COMM_WORLD.Abort(MPI::ERR_OTHER);
  
  // Should never get here

  if (my_rank <= 0)
    cerr << "MPI2C++ test suite: terminated" << endl << endl;

  exit(MPI::ERR_OTHER);
}


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

#include <iostream.h>
#include "mpi++.h"
#include "mpi2c++_test.h"
#if MPI2CPP_SGI30
extern "C" {
#include <string.h>
}
#endif
#if MPI2CPP_CRAY
#include <mpp/rastream.h>
#endif

//
// Global variables
//

int comm_size = -1;
int my_rank = -1;
int to = -1;
int from = -1;
MPI2CPP_BOOL_T CANCEL_WORKS = MPI2CPP_FALSE;
MPI2CPP_BOOL_T TIGHTLY_COUPLED = MPI2CPP_FALSE;
const int version[2] = {1, 5};
const double Epsilon = 0.001;
MPI2CPP_BOOL_T flags[SKIP_MAX];


//
// Local functions
//

static void check_args(int argc, char *argv[]);
static int my_strcasecmp(const char *a, const char *b);
static void check_minimals(void);


//
// main
//
int
main(int argc, char *argv[])
{
#if MPI2CPP_CRAY
  int oldstr = get_d_stream();
  set_d_stream(0);
#endif

  // Start up MPI

  check_args(argc, argv);

  initialized1();

  MPI::Init(argc, argv);

  // Define some globals

  comm_size = MPI::COMM_WORLD.Get_size();
  my_rank = MPI::COMM_WORLD.Get_rank();
  to = (my_rank + 1) % comm_size;
  from = (my_rank + comm_size - 1) % comm_size;

  // Announce

  if (my_rank == 0)
    cout << endl
	 << "Since we made it this far, we will assume that" << endl
	 << "MPI::Init() worked properly." << endl 
	 << "----------------------------------------------" 
	 << endl 
	 << "MPI-2 C++ bindings test suite" << endl 
	 << "------------------------------" << endl 
	 << "LSC Version " 
	 << version[0] << "." << version[1] << endl 
	 << endl 
	 << "*** There are delays built into some of the tests" << endl 
	 << "*** Please let them complete" << endl 
	 << "*** No test should take more than 10 seconds" << endl << endl;

  // Catch all fatal signals

  signal_init();

  // Check for minimal testing conditions in MPI environment

  check_minimals();

  // Ensure that all the ranks have the relevant command line args
  // That is, pass on any of the _flag arguments

#if _MPIPP_BOOL_NE_INT_
  MPI::COMM_WORLD.Bcast(flags, SKIP_MAX * sizeof(MPI2CPP_BOOL_T), MPI::CHAR, 0);
#else
  MPI::COMM_WORLD.Bcast(flags, SKIP_MAX, MPI::INT, 0);
#endif
#define HANG 1
#if HANG
  // Test all the objects
  // WDG - Make "xxx" a char * instead of a String
  Testing("MPI namespace");
  initialized2();
  procname();
  Pass(); // MPI namespace

  Testing("MPI::Comm");
  rank_size();
  Pass(); // MPI::Comm

  Testing("MPI::Status");
  status_test();
  Pass(); // MPI::Status

  Testing("MPI::Comm");
  send();
  errhandler();
  Pass(); // MPI::Comm

  Testing("MPI::Request");
  request1();
  Pass(); // MPI::Request

  Testing("MPI::Status");
  getcount();
  getel();
  Pass(); // MPI::Status

  Testing("MPI namespace");
  buffer();
  dims();
  pcontrol();
  wtime();
  Pass(); // MPI namespace

  Testing("MPI::Comm");
  topo();
  bsend();
  rsend();
  ssend();
  isend();
  sendrecv();
  sendrecv_rep();
  iprobe();
  probe();
  Pass(); // MPI::Comm

  Testing("MPI::Request");
  waitany();
  testany();
  waitall();
  testall();
  waitsome();
  testsome();
  cancel();
  Pass(); // MPI::Request

  Testing("MPI::Comm");
  start();
  startall();
  Pass(); // MPI::Comm
#endif

  Testing("MPI::Intracomm");
#if HANG
  dup_test();
  bcast();
  gather();
  struct_gatherv();
  scatter();
  allgather();
  alltoall();
  reduce();
  allreduce();
  reduce_scatter();
#endif
  scan();
#if HANG
  split();
#endif
  Pass(); // MPI::Intracomm

  Testing("MPI::Cartcomm");
#if HANG
  cartcomm(); 
#endif
  Pass(); // MPI::Cartcomm

  Testing("MPI::Graphcomm");
  graphcomm();
  Pass(); // MPI::Graphcomm

#if HANG
  Testing("MPI::Datatype");
  bcast_struct();
  pack_test();
  Pass(); // MPI::Datatype

  Testing("MPI::Intracomm");
  compare();
  Pass(); // MPI::Intracomm
#endif

  Testing("MPI::");
  intercomm1();
  Pass(); // MPI::

  Testing("MPI::Comm");
  attr();
  Pass(); // MPI::Comm

  Testing("MPI::Group");
  group();
  groupfree();
  Pass(); // MPI::Group

  Testing("MPI::Op");
  op_test();
  Pass(); // MPI::Op 

  // All done.  Call MPI_Finalize()

  if (my_rank == 0)
    cout << endl << "* MPI::Finalize..." << endl;

  MPI::COMM_WORLD.Barrier();

  MPI::Finalize();

  if (my_rank == 0)
    cout << endl << endl
	 << "Since we made it this far, we will assume that" << endl
	 << "MPI::Finalize() did what we wanted it to." << endl
	 << "(Or, at the very least, it didn't fail.)" << endl
	 << "-----------------------------------------------------------" << endl
	 << "MPI-2 C++ bindings test suite: All done.  All tests passed." << endl
	 << endl;

#if MPI2CPP_CRAY
  set_d_stream(oldstr);
#endif

  return 0;
}


//
// Check the args for command line flags
//
void
check_args(int argc, char *argv[])
{
  int i;

  for (i = 0; i <= SKIP_MAX; i++)
    flags[i] = MPI2CPP_FALSE;

  for (i = 1; i < argc; i++) {
    if (my_strcasecmp(argv[i], "-lam63") == 0)
      flags[SKIP_LAM63] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-lam64") == 0)
      flags[SKIP_LAM64] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-ibm21014") == 0)
      flags[SKIP_IBM21014] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-ibm21015") == 0)
      flags[SKIP_IBM21015] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-ibm21016") == 0)
      flags[SKIP_IBM21016] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-ibm21017") == 0)
      flags[SKIP_IBM21017] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-ibm21018") == 0)
      flags[SKIP_IBM21018] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-ibm2300") == 0)
      flags[SKIP_IBM2_3_0_0] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-sgi20") == 0)
      flags[SKIP_SGI20] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-sgi30") == 0)
      flags[SKIP_SGI30] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-sgi31") == 0)
      flags[SKIP_SGI31] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-sgi32") == 0)
      flags[SKIP_SGI32] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-hpux0102") == 0)
      flags[SKIP_HPUX0102] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-cray1104") == 0)
      flags[SKIP_CRAY1104] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-nothrow") == 0)
      flags[SKIP_NO_THROW] = MPI2CPP_TRUE;
    else if (my_strcasecmp(argv[i], "-help") == 0 ||
	     my_strcasecmp(argv[i], "-h") == 0) {
      cout << "The following command line options are available:" << endl 
	   << " -help        This message" << endl 
	   << " -lam62       Skip tests for buggy LAM 6.2" << endl 
	   << " -lam63       Skip tests for buggy LAM 6.3.x" << endl 
	   << " -lam63       Skip tests for buggy LAM 6.4.x" << endl 
	   << " -ibm21014    Skip tests for buggy IBM SP MPI 2.1.0.14" << endl
	   << " -ibm21015    Skip tests for buggy IBM SP MPI 2.1.0.15" << endl
	   << " -ibm21016    Skip tests for buggy IBM SP MPI 2.1.0.16" << endl
	   << " -ibm21017    Skip tests for buggy IBM SP MPI 2.1.0.17" << endl
	   << " -ibm21018    Skip tests for buggy IBM SP MPI 2.1.0.18" << endl
	   << " -ibm2300     Skip tests for buggy IBM SP MPI 2.3.0.0" << endl
	   << " -sgi20       Skip tests for buggy SGI MPI 2.0" << endl
	   << " -sgi30       Skip tests for buggy SGI MPI 3.0" << endl
	   << " -sgi31       Skip tests for buggy SGI MPI 3.1" << endl
	   << " -sgi32       Skip tests for buggy SGI MPI 3.2" << endl
	   << " -hpux0102    Skip tests for buggy HP-UX MPI 1.02" << endl
	   << " -cray1104    Skip tests for buggy CRAY MPI 1.1.0.4" << endl
	   << " -nothrow     Skip exception tests for buggy compilers" << endl;

      exit(0);
    }
  }
}


int
my_strcasecmp(const char *a, const char *b)
{
  while ((a != 0) && (b != 0) && (*a != '\0') && (*b != '\0') && (*a == *b))
    a++, b++;
  if (*a == *b)
    return 0;
  return 1;
}


//
// Do meaningless work to burn up time
//
void
do_work(int top)
{
  double start1 = MPI::Wtime() + .25;
  top++;

  while(MPI::Wtime() < start1)
    continue;
}


//
// Check for minimal MPI environment
//
static void
check_minimals()
{
  MPI2CPP_BOOL_T need_flag = MPI2CPP_FALSE;
  /*const*/ char *msg = (char*) "";

  if (my_rank == 0)
    cout << "Test suite running on " << comm_size << " nodes" << endl;

  if (comm_size < 2) {
    if (my_rank == 0) {
      cout << "Sorry, the MPI2C++ test suite must be run with at least 2 processors" << endl;
      cout << "Please re-run the program with 2 or more processors." << 
	endl << endl;
    }
    MPI::Finalize();
    exit(1);
  }
  
  if ((comm_size % 2) != 0) {
    if (my_rank == 0)
      cout << "The MPI2C++ test suite can only run on an even number" << endl 
	   << "of processors.  Please re-run the program with an even" << endl 
	   << "number of ranks." << endl << endl;

    MPI::Finalize();
    exit(1);
  }

  // Check to see if we *should* be using one of the above flags

#if MPI2CPP_LAM63
  if (!flags[SKIP_LAM63]) {
    need_flag = MPI2CPP_TRUE;
    msg = (char*) "-lam63";
  }
#elif MPI2CPP_LAM64
  if (!flags[SKIP_LAM64]) {
    need_flag = MPI2CPP_TRUE;
    msg = (char*) "-lam64";
  }
#elif MPI2CPP_IBM21014
  if (!flags[SKIP_IBM21014]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-ibm21014";
  }
#elif MPI2CPP_IBM21015
  if (!flags[SKIP_IBM21015]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-ibm21015";
  }
#elif MPI2CPP_IBM21016
  if (!flags[SKIP_IBM21016]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-ibm21016";
  }
#elif MPI2CPP_IBM21017
  if (!flags[SKIP_IBM21017]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-ibm21017";
  }
#elif MPI2CPP_IBM21018
  if (!flags[SKIP_IBM21018]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-ibm21018";
  }
#elif MPI2CPP_IBM2_3_0_0
  if (!flags[SKIP_IBM2_3_0_0]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-ibm2300";
  }
#elif MPI2CPP_SGI20
  if (!flags[SKIP_SGI20]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-sgi20";
  }
#elif MPI2CPP_SGI30
  if (!flags[SKIP_SGI30]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-sgi30";
  }
#elif MPI2CPP_SGI31
  if (!flags[SKIP_SGI31]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-sgi31";
  }
#elif MPI2CPP_SGI32
  if (!flags[SKIP_SGI32]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-sgi32";
  }
#elif MPI2CPP_HPUX0102
  if (!flags[SKIP_HPUX0102]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-hpux0102";
  }
#elif MPI2CPP_HPUX0103
  if (!flags[SKIP_HPUX0103]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-hpux0103";
  }
#elif MPI2CPP_CRAY1104
  if (!flags[SKIP_CRAY1104]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-cray1104";
  }
#endif

  if (need_flag && my_rank == 0) {
    cout << "**** WARNING!! ****" << endl << endl
	 << "You really should use the \"" << msg << "\" flag when running the " 
	 << endl 
	 << "test suite on this architecture/OS.  If you do not use this flag," 
	 << endl
	 << "certain tests will probably fail, and the test suite will abort." 
	 << endl << endl
	 << "The test suite will now commence without this flag so that you " 
	 << endl
	 << "can see which tests will fail on this architecture/OS." 
	 << endl << endl;
  }

  need_flag = MPI2CPP_FALSE;
#if MPI2CPP_G_PLUS_PLUS
  if (!flags[SKIP_NO_THROW]) {
    need_flag = MPI2CPP_TRUE;
    msg = "-nothrow";
  }
#endif

  if (need_flag && my_rank == 0) {
    cout << "**** WARNING!! ****" << endl << endl
	 << "You really should use the \"" << msg << "\" flag when running the " 
	 << endl 
	 << "test suite on this architecture/OS.  If you do not use this flag," 
	 << endl
	 << "certain tests will probably fail, and the test suite will abort." 
	 << endl << endl
	 << "The test suite will now commence without this flag so that you " 
	 << endl
	 << "can see which tests will fail on this architecture/OS." 
	 << endl << endl;
  }
}

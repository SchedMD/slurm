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
// I made this.
#include "mpi2c++_test.h"

void
status_test()
{
  char msg[150];
  int get_err;
  int get_src;
  int get_tag;
  int set_err;
  int set_src;
  int set_tag;
  MPI::Status status;

  get_err = 0;
  get_src = 0;
  get_tag = 0;
  set_err = 1;
  set_src = 2;
  set_tag = 3;

  Testing("Set_source / Get_source");

  status.Set_source(set_src);
  get_src = status.Get_source();
  if(get_src != set_src) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Status, source = %d, should be %d",
	    my_rank, get_src, set_src);
    Fail(msg);
  }

  Pass(); // Set_source / Get_source

  Testing("Set_tag / Get_tag");

  status.Set_tag(set_tag);
  get_tag = status.Get_tag();
  if(get_tag != set_tag) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Status, tag = %d, should be %d",
	    my_rank, get_tag, set_tag);
    Fail(msg);
  }

  Pass(); // Set_tag / Get_tag

  Testing("Set_error / Get_error");

  status.Set_error(set_err);
  get_err = status.Get_error();
  if(get_err != set_err) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Status, error = %d, should be %d",
	    my_rank, get_err, set_err);
    Fail(msg);
  }

  Pass(); // Set_error / Get_error
}

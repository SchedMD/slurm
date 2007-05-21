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
/****************************************************************************

 MESSAGE PASSING INTERFACE TEST CASE SUITE

 Copyright IBM Corp. 1995

 IBM Corp. hereby grants a non-exclusive license to use, copy, modify, and
 distribute this software for any purpose and without fee provided that the
 above copyright notice and the following paragraphs appear in all copies.

 IBM Corp. makes no representation that the test cases comprising this
 suite are correct or are an accurate representation of any standard.

 In no event shall IBM be liable to any party for direct, indirect, special
 incidental, or consequential damage arising out of the use of this software
 even if IBM Corp. has been advised of the possibility of such damage.

 IBM CORP. SPECIFICALLY DISCLAIMS ANY WARRANTIES INCLUDING, BUT NOT LIMITED
 TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS AND IBM
 CORP. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 ENHANCEMENTS, OR MODIFICATIONS.

****************************************************************************

 These test cases reflect an interpretation of the MPI Standard.  They are
 are, in most cases, unit tests of specific MPI behaviors.  If a user of any
 test case from this set believes that the MPI Standard requires behavior
 different than that implied by the test case we would appreciate feedback.

 Comments may be sent to:
    Richard Treumann
    treumann@kgn.ibm.com

****************************************************************************
*/

#include "mpi2c++_test.h"

void
getel()
{
  char msg[150];
  int count;
  int data[100];
  int i;
  MPI::Status status;

  for(i = 0; i < 100; i++)
    data[i] = -1;

  Testing("Get_elements");

  if((my_rank % 2) == 0) {
    MPI::COMM_WORLD.Send(&data, 5, MPI::BYTE, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::CHAR, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::INT, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::FLOAT, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::DOUBLE, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::LONG_DOUBLE, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::SHORT, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::LONG, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::PACKED, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::UNSIGNED_CHAR, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::UNSIGNED_SHORT, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::UNSIGNED, my_rank + 1, 1);
    MPI::COMM_WORLD.Send(&data, 5, MPI::UNSIGNED_LONG, my_rank + 1, 1);
  } else if((my_rank % 2) == 1) {
    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::BYTE, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::BYTE);
    if(count != 5) {
      sprintf(msg, "NODE %d - 1) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::CHAR, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::CHAR);
    if(count != 5) {
      sprintf(msg, "NODE %d - 2) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::INT, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::INT);
    if(count != 5) {
      sprintf(msg, "NODE %d - 3) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::FLOAT, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::FLOAT);
    if(count != 5) {
      sprintf(msg, "NODE %d - 4) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::DOUBLE, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::DOUBLE);
    if(count != 5) {
      sprintf(msg, "NODE %d - 5) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::LONG_DOUBLE, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::LONG_DOUBLE);
    if(count != 5) {
      sprintf(msg, "NODE %d - 6) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::SHORT, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::SHORT);
    if(count != 5) {
      sprintf(msg, "NODE %d - 7) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::LONG, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::LONG);
    if(count != 5) {
      sprintf(msg, "NODE %d - 8) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::PACKED, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::PACKED);
    if(count != 5) {
      sprintf(msg, "NODE %d - 9) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::UNSIGNED_CHAR, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::UNSIGNED_CHAR);
    if(count != 5) {
      sprintf(msg, "NODE %d - 10) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::UNSIGNED_SHORT, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::UNSIGNED_SHORT);
    if(count != 5) {
      sprintf(msg, "NODE %d - 11) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::UNSIGNED, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::UNSIGNED);
    if(count != 5) {
      sprintf(msg, "NODE %d - 12) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
    for(i = 0; i < count; i++)
      data[i] = -1;

    count = 0;
    MPI::COMM_WORLD.Recv(&data, 5, MPI::UNSIGNED_LONG, my_rank - 1, 1, status);
    count = status.Get_elements(MPI::UNSIGNED_LONG);
    if(count != 5) {
      sprintf(msg, "NODE %d - 13) ERROR in MPI::Get_elements, count = %d, should be %d", my_rank, count, 5);
      Fail(msg);
    }
  }
  
  Pass(); // Get_elements
}



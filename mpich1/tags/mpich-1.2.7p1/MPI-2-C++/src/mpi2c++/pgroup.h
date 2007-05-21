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

class Group {
public:
  // construction / destruction
  inline Group() : mpi_group(MPI_GROUP_NULL) { }
  inline virtual ~Group() {}
  inline Group(const MPI_Group &i) : mpi_group(i) { }

  // copy / assignment
  Group(const Group& g): mpi_group(g.mpi_group) { }

  Group& operator=(const Group& g) {
    mpi_group = g.mpi_group;
    return *this;
  }

  // comparison
  MPI2CPP_BOOL_T operator== (const Group &a) {
    return (MPI2CPP_BOOL_T)(mpi_group == a.mpi_group);
  }
  MPI2CPP_BOOL_T operator!= (const Group &a) { return (MPI2CPP_BOOL_T)!(*this == a); }
 
  // inter-language operability
  inline Group& operator= (const MPI_Group &i) { mpi_group = i; return *this; }
  inline operator const MPI_Group& () const { return mpi_group; }
  inline operator MPI_Group* () const { return (MPI_Group*)&mpi_group; }

  inline const MPI_Group& mpi() const { return mpi_group; }
  //
  // Groups, Contexts, and Communicators
  //

  virtual int Get_size() const;
  
  virtual int Get_rank() const;

  static void Translate_ranks(const Group& group1, int n, const int ranks1[], 
			      const Group& group2, int ranks2[]);
  
  static int Compare(const Group& group1, const Group& group2);

  static Group Union(const Group &group1, const Group &group2);

  static Group Intersect(const Group &group1, const Group &group2);

  static Group Difference(const Group &group1, const Group &group2);

  virtual Group Incl(int n, const int ranks[]) const;

  virtual Group Excl(int n, const int ranks[]) const;

  virtual Group Range_incl(int n, const int ranges[][3]) const;

  virtual Group Range_excl(int n, const int ranges[][3]) const;

  virtual void Free();

protected:
  MPI_Group mpi_group;

};


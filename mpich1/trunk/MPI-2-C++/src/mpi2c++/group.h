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
#if _MPIPP_PROFILING_
  //  friend class PMPI::Group;
#endif
public:

#if _MPIPP_PROFILING_

  // construction
  inline Group() { }
  inline Group(const MPI_Group &i) : pmpi_group(i) { }
  // copy
  inline Group(const Group& g) : pmpi_group(g.pmpi_group) { }

  inline Group(const PMPI::Group& g) : pmpi_group(g) { }

  inline virtual ~Group() {}

  Group& operator=(const Group& g) {
    pmpi_group = g.pmpi_group; return *this;
  }

  // comparison
  inline MPI2CPP_BOOL_T operator== (const Group &a) {
    return (MPI2CPP_BOOL_T)(pmpi_group == a.pmpi_group);
  }
  inline MPI2CPP_BOOL_T operator!= (const Group &a) { 
    return (MPI2CPP_BOOL_T)!(*this == a);
  }
 
  // inter-language operability
  Group& operator= (const MPI_Group &i) { pmpi_group = i; return *this; }
  inline operator MPI_Group () const { return pmpi_group.mpi(); }
  //  inline operator MPI_Group* () const { return pmpi_group; }
  inline operator const PMPI::Group&() const { return pmpi_group; }

  const PMPI::Group& pmpi() { return pmpi_group; }
#else

  // construction
  inline Group() : mpi_group(MPI_GROUP_NULL) { }
  inline Group(const MPI_Group &i) : mpi_group(i) { }

  // copy
  inline Group(const Group& g) : mpi_group(g.mpi_group) { }

  inline virtual ~Group() {}

  inline Group& operator=(const Group& g) { mpi_group = g.mpi_group; return *this; }

  // comparison
  inline MPI2CPP_BOOL_T operator== (const Group &a) { return (MPI2CPP_BOOL_T)(mpi_group == a.mpi_group); }
  inline MPI2CPP_BOOL_T operator!= (const Group &a) { return (MPI2CPP_BOOL_T)!(*this == a); }
 
  // inter-language operability
  inline Group& operator= (const MPI_Group &i) { mpi_group = i; return *this; }
  inline operator MPI_Group () const { return mpi_group; }
  //  inline operator MPI_Group* () const { return (MPI_Group*)&mpi_group; }

  inline MPI_Group mpi() const { return mpi_group; }

#endif

  //
  // Groups, Contexts, and Communicators
  //

  virtual int Get_size() const;
  
  virtual int Get_rank() const;
  
  static void Translate_ranks (const Group& group1, int n, const int ranks1[], 
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
#if ! _MPIPP_PROFILING_
  MPI_Group mpi_group;
#endif

#if _MPIPP_PROFILING_
private:
  PMPI::Group pmpi_group;
#endif

};


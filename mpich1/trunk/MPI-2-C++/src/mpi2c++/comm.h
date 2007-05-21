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


class Comm_Null {
#if _MPIPP_PROFILING_
  //  friend class PMPI::Comm_Null;
#endif
public:

#if _MPIPP_PROFILING_

  // construction
  inline Comm_Null() { }
  // copy
  inline Comm_Null(const Comm_Null& data) : pmpi_comm(data.pmpi_comm) { }
  // inter-language operability  
  inline Comm_Null(const MPI_Comm& data) : pmpi_comm(data) { }

  inline Comm_Null(const PMPI::Comm_Null& data) : pmpi_comm(data) { }

  // destruction
  //JGS  virtual inline ~Comm_Null() { }

  inline Comm_Null& operator=(const Comm_Null& data) {
    pmpi_comm = data.pmpi_comm; 
    return *this;
  }

  // comparison
  inline MPI2CPP_BOOL_T operator==(const Comm_Null& data) const {
    return (MPI2CPP_BOOL_T) (pmpi_comm == data.pmpi_comm); }

  inline MPI2CPP_BOOL_T operator!=(const Comm_Null& data) const {
    return (MPI2CPP_BOOL_T) (pmpi_comm != data.pmpi_comm);}

  // inter-language operability (conversion operators)
  inline operator MPI_Comm() const { return pmpi_comm; }
  //  inline operator MPI_Comm*() /*const JGS*/ { return pmpi_comm; }
  inline operator const PMPI::Comm_Null&() const { return pmpi_comm; }

#else

  // construction
  inline Comm_Null() : mpi_comm(MPI_COMM_NULL) { }
  // copy
  inline Comm_Null(const Comm_Null& data) : mpi_comm(data.mpi_comm) { }
  // inter-language operability  
  inline Comm_Null(const MPI_Comm& data) : mpi_comm(data) { }

  // destruction
  //JGS virtual inline ~Comm_Null() { }

 // comparison
  // JGS make sure this is right (in other classes too)
  inline MPI2CPP_BOOL_T operator==(const Comm_Null& data) const {
    return (MPI2CPP_BOOL_T) (mpi_comm == data.mpi_comm); }

  inline MPI2CPP_BOOL_T operator!=(const Comm_Null& data) const {
    return (MPI2CPP_BOOL_T) !(*this == data);}

  // inter-language operability (conversion operators)
  inline operator MPI_Comm() const { return mpi_comm; }

#endif

  
protected:

#if _MPIPP_PROFILING_
  PMPI::Comm_Null pmpi_comm;
#else
  MPI_Comm mpi_comm;
#endif
  

};


class Comm : public Comm_Null {
public:

  typedef void Errhandler_fn(Comm&, int*, ...);
  typedef int Copy_attr_function(const Comm& oldcomm, int comm_keyval,
				 void* extra_state, void* attribute_val_in,
				 void* attribute_val_out, 
				 MPI2CPP_BOOL_T& flag);
  typedef int Delete_attr_function(Comm& comm, int comm_keyval, 
				   void* attribute_val,
				   void* extra_state);
#if !_MPIPP_PROFILING_
#define _MPI2CPP_ERRHANDLERFN_ Errhandler_fn
#define _MPI2CPP_COPYATTRFN_ Copy_attr_function
#define _MPI2CPP_DELETEATTRFN_ Delete_attr_function
#endif

  // construction
  Comm() { }

  // copy
  Comm(const Comm_Null& data) : Comm_Null(data) { }

#if _MPIPP_PROFILING_
  // WDG - The base constructure should come first (g++ complains)
  Comm(const Comm& data) : Comm_Null(data), pmpi_comm((const PMPI::Comm&) data)  { }

  // inter-language operability
  Comm(const MPI_Comm& data) : Comm_Null(data), pmpi_comm(data)  { }

  Comm(const PMPI::Comm& data) : Comm_Null((const PMPI::Comm_Null&)data), pmpi_comm(data)  { }

  operator const PMPI::Comm&() const { return pmpi_comm; }

  // assignment
  Comm& operator=(const Comm& data) {
    this->Comm_Null::operator=(data);
    pmpi_comm = data.pmpi_comm; 
    return *this;
  }
  Comm& operator=(const Comm_Null& data) {
    this->Comm_Null::operator=(data);
    MPI_Comm tmp = data;
    pmpi_comm = tmp; 
    return *this;
  }
  // inter-language operability
  Comm& operator=(const MPI_Comm& data) {
    this->Comm_Null::operator=(data);
    pmpi_comm = data;
    return *this;
  }

#else
  Comm(const Comm& data) : Comm_Null(data.mpi_comm) { }
  // inter-language operability
  Comm(const MPI_Comm& data) : Comm_Null(data) { }
#endif


  //
  // Point-to-Point
  //

  virtual void Send(const void *buf, int count, 
		    const Datatype & datatype, int dest, int tag) const;

  virtual void Recv(void *buf, int count, const Datatype & datatype,
		    int source, int tag, Status & status) const;


  virtual void Recv(void *buf, int count, const Datatype & datatype,
		    int source, int tag) const;
  
  virtual void Bsend(const void *buf, int count,
		     const Datatype & datatype, int dest, int tag) const;
  
  virtual void Ssend(const void *buf, int count, 
		     const Datatype & datatype, int dest, int tag) const ;

  virtual void Rsend(const void *buf, int count,
		     const Datatype & datatype, int dest, int tag) const;
  
  virtual Request Isend(const void *buf, int count,
			const Datatype & datatype, int dest, int tag) const;
  
  virtual Request Ibsend(const void *buf, int count, const
			 Datatype & datatype, int dest, int tag) const;
  
  virtual Request Issend(const void *buf, int count,
			 const Datatype & datatype, int dest, int tag) const;
  
  virtual Request Irsend(const void *buf, int count,
			 const Datatype & datatype, int dest, int tag) const;

  virtual Request Irecv(void *buf, int count,
			const Datatype & datatype, int source, int tag) const;

  virtual MPI2CPP_BOOL_T Iprobe(int source, int tag, Status & status) const;

  virtual MPI2CPP_BOOL_T Iprobe(int source, int tag) const;

  virtual void Probe(int source, int tag, Status & status) const;
  
  virtual void Probe(int source, int tag) const;
  
  virtual Prequest Send_init(const void *buf, int count,
			     const Datatype & datatype, int dest, 
			     int tag) const;
  
  virtual Prequest Bsend_init(const void *buf, int count,
			      const Datatype & datatype, int dest, 
			      int tag) const;
  
  virtual Prequest Ssend_init(const void *buf, int count,
			      const Datatype & datatype, int dest, 
			      int tag) const;
  
  virtual Prequest Rsend_init(const void *buf, int count,
			      const Datatype & datatype, int dest, 
			      int tag) const;
  
  virtual Prequest Recv_init(void *buf, int count,
			     const Datatype & datatype, int source, 
			     int tag) const;
  
  virtual void Sendrecv(const void *sendbuf, int sendcount,
			const Datatype & sendtype, int dest, int sendtag, 
			void *recvbuf, int recvcount, 
			const Datatype & recvtype, int source,
			int recvtag, Status & status) const;
  
  virtual void Sendrecv(const void *sendbuf, int sendcount,
			const Datatype & sendtype, int dest, int sendtag, 
			void *recvbuf, int recvcount, 
			const Datatype & recvtype, int source,
			int recvtag) const;

  virtual void Sendrecv_replace(void *buf, int count,
				const Datatype & datatype, int dest, 
				int sendtag, int source,
				int recvtag, Status & status) const;

  virtual void Sendrecv_replace(void *buf, int count,
				const Datatype & datatype, int dest, 
				int sendtag, int source,
				int recvtag) const;
  
  //
  // Groups, Contexts, and Communicators
  //

  virtual Group Get_group() const;
  
  virtual int Get_size() const;

  virtual int Get_rank() const;
  
  static int Compare(const Comm & comm1, const Comm & comm2);
  
  virtual Comm& Clone() const = 0;

  virtual void Free(void);
  
  virtual MPI2CPP_BOOL_T Is_inter() const;
  
  //
  //Process Topologies
  //
  
  virtual int Get_topology() const;
  
  //
  // Environmental Inquiry
  //
  
  virtual void Abort(int errorcode);

  //
  // Errhandler
  //

  virtual void Set_errhandler(const Errhandler& errhandler);

  virtual Errhandler Get_errhandler() const;

  //JGS took out const below from fn arg
  static Errhandler Create_errhandler(Comm::Errhandler_fn* function);

  //
  // Keys and Attributes
  //

//JGS I took the const out because it causes problems when trying to
//call this function with the predefined NULL_COPY_FN etc.
  static int Create_keyval(Copy_attr_function* comm_copy_attr_fn,
			   Delete_attr_function* comm_delete_attr_fn,
			   void* extra_state);
  
  static void Free_keyval(int& comm_keyval);

  virtual void Set_attr(int comm_keyval, const void* attribute_val) const;

  virtual MPI2CPP_BOOL_T Get_attr(int comm_keyval, void* attribute_val) const;
  
  virtual void Delete_attr(int comm_keyval);

  static int NULL_COPY_FN(const Comm& oldcomm, int comm_keyval,
			  void* extra_state, void* attribute_val_in,
			  void* attribute_val_out, MPI2CPP_BOOL_T& flag);
  
  static int DUP_FN(const Comm& oldcomm, int comm_keyval,
		    void* extra_state, void* attribute_val_in,
		    void* attribute_val_out, MPI2CPP_BOOL_T& flag);
  
  static int NULL_DELETE_FN(Comm& comm, int comm_keyval, void* attribute_val,
			    void* extra_state);


  //#if _MPIPP_PROFILING_
  //  virtual const PMPI::Comm& get_pmpi_comm() const { return pmpi_comm; }
  //#endif

private:
#if _MPIPP_PROFILING_
  PMPI::Comm pmpi_comm;
#endif

#if !MPI2CPP_HAVE_STATUS_IGNORE
  static Status ignored_status;
#endif

#if ! _MPIPP_PROFILING_
public: // JGS hmmm, these used by errhandler_intercept
        // should make it a friend

  Errhandler* my_errhandler;

  typedef Map<Comm*, CommType>::Pair comm_pair_t;
  typedef Map<MPI_Comm, comm_pair_t*> mpi_comm_map_t;
  static mpi_comm_map_t mpi_comm_map;

  typedef Map<MPI_Comm, Comm*> mpi_err_map_t;
  static mpi_err_map_t mpi_err_map;

  typedef Map<Comm::_MPI2CPP_COPYATTRFN_*, Comm::_MPI2CPP_DELETEATTRFN_*>::Pair key_pair_t;
  typedef Map<int, key_pair_t*> key_fn_map_t;
  static key_fn_map_t key_fn_map;

  void init() {
    my_errhandler = (Errhandler*)0;
  }
#endif

};

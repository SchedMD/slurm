void Barrier() const { MPIX_CALL( MPI_Barrier( the_real_comm ) ); }
void Bcast(void* buffer, int count, const Datatype& datatype, 
	   int root) const;
void Gather(const void* sendbuf, int sendcount, const Datatype& sendtype, 
	    void* recvbuf, int recvcount, const Datatype& recvtype, 
	    int root) const;
void Gatherv(const void* sendbuf, int sendcount, const Datatype& sendtype, 
	     void* recvbuf, const int recvcounts[],  const int displs[], 
	     const Datatype& recvtype, int root) const;
void Scatter(const void* sendbuf, int sendcount, const Datatype& sendtype, 
	     void* recvbuf, int recvcount, const Datatype& recvtype, 
	     int root) const;
void Scatterv(const void* sendbuf, const int sendcounts[], const int displs[], 
	      const Datatype& sendtype, void* recvbuf, int recvcount, 
	      const Datatype& recvtype, int root) const;
void Allgather(const void* sendbuf, int sendcount, const Datatype& sendtype, 
	       void* recvbuf, int recvcount, const Datatype& recvtype) const;
void Allgatherv(const void* sendbuf, int sendcount, const Datatype& sendtype,
		void* recvbuf, const int recvcounts[], const int displs[], 
		const Datatype& recvtype) const;
void Alltoall(const void* sendbuf, int sendcount, const Datatype& sendtype, 
	      void* recvbuf, int recvcount, const Datatype& recvtype) const;
void Alltoallv(const void* sendbuf, const int sendcounts[], 
	       const int sdispls[], const Datatype& sendtype, void* recvbuf, 
	       const int recvcounts[], const int rdispls[], 
	       const Datatype& recvtype) const;
void Reduce(const void* sendbuf, void* recvbuf, int count, 
	    const Datatype& datatype, const Op& op, int root) const;
void Allreduce(const void* sendbuf, void* recvbuf, int count, 
	       const Datatype& datatype, const Op& op) const;
void Reduce_scatter(const void* sendbuf, void* recvbuf, int recvcounts[], 
		    const Datatype& datatype, const Op& op) const;
void Scan(const void* sendbuf, void* recvbuf, int count, 
	  const Datatype& datatype, const Op& op) const;
Intracomm Dup() const { Intracomm *c = new(Intracomm); 
    MPIX_CALL( MPI_Comm_dup( the_real_comm, &c->the_real_comm ) ); 
    return *c; }
Intracomm Create(const Group& group) const;
Intracomm Split(int color, int key) const;
Intercomm Create_intercomm(int local_leader, const Comm& peer_comm, 
			   int remote_leader, int tag) const;
Cartcomm Create_cart(int ndims, const int dims[], const bool periods[], 
		     bool reorder) const;
Graphcomm Create_graph(int nnodes, const int index[], const int edges[], 
		       bool reorder) const;

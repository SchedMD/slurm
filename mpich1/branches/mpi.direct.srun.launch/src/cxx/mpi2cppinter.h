static Intercomm Get_parent();
static Intercomm Join(const int fd);

Intercomm Create(const Group& group) const;
Intercomm Split(int color, int key) const;
void Allgather(const void* sendbuf, int sendcount, const MPI::Datatype& sendtype, void* recvbuf, int recvcount, const MPI::Datatype& recvtype) const;
void Allgatherv(const void* sendbuf, int sendcount,  const MPI::Datatype& sendtype, void* recvbuf,  const int recvcounts[], const int displs[],  const MPI::Datatype& recvtype) const;
void Allreduce(const void* sendbuf, void* recvbuf,  int count, const MPI::Datatype& datatype, const MPI::Op& op) const;
void Alltoall(const void* sendbuf, int sendcount, const MPI::Datatype& sendtype, void* recvbuf, int recvcount, const MPI::Datatype& recvtype) const;
void Alltoallv(const void* sendbuf,  const int sendcounts[], const int sdispls[],  const MPI::Datatype& sendtype, void* recvbuf,  const int recvcounts[], const int rdispls[],  const MPI::Datatype& recvtype) const;
void Alltoallw(const void* sendbuf, const int sendcounts[], const int sdispls[], const MPI::Datatype sendtypes[], void* recvbuf, const int recvcounts[], const int rdispls[], const MPI::Datatype recvtypes[]) const;
void Barrier() const;
void Bcast(void* buffer, int count,  const MPI::Datatype& datatype, int root) const;
void Gather(const void* sendbuf, int sendcount,  const MPI::Datatype& sendtype, void* recvbuf, int recvcount,  const MPI::Datatype& recvtype, int root) const;
void Gatherv(const void* sendbuf, int sendcount,  const MPI::Datatype& sendtype, void* recvbuf, const int recvcounts[], const int displs[], const MPI::Datatype& recvtype, int root) const;
void Reduce(const void* sendbuf, void* recvbuf,  int count, const MPI::Datatype& datatype, const MPI::Op& op,  int root) const;
void Reduce_scatter(const void* sendbuf,  void* recvbuf, int recvcounts[], const MPI::Datatype& datatype,  const MPI::Op& op) const;
void Scatter(const void* sendbuf, int sendcount,  const MPI::Datatype& sendtype, void* recvbuf, int recvcount,  const MPI::Datatype& recvtype, int root) const;
void Scatterv(const void* sendbuf, const int sendcounts[], const int displs[], const MPI::Datatype& sendtype, void* recvbuf, int recvcount, const MPI::Datatype& recvtype, int root) const;

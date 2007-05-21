void Send(const void* buf, int count, const Datatype& datatype, int dest, 
	  int tag) const { 
  MPIX_CALL( MPI_Send( (void *)buf, count, 
		       (MPI_Datatype)datatype.the_real_dtype, 
		       dest, tag, the_real_comm )); }
void Recv(void* buf, int count, const Datatype& datatype, int source, 
          int tag,  Status& status) const {
  MPIX_CALL( MPI_Recv( buf, count, (MPI_Datatype)datatype.the_real_dtype, 
		       source, tag, 
		       the_real_comm, (MPI_Status*)status ));}
void Recv(void* buf, int count, const Datatype& datatype, int source, 
	  int tag) const {
  MPIX_CALL( MPI_Recv( buf, count, (MPI_Datatype)datatype.the_real_dtype, 
		       source, tag,  
		       the_real_comm, MPI_STATUS_IGNORE ));}
void Bsend(const void* buf, int count, const Datatype& datatype, int dest, 
	   int tag) const;
void Ssend(const void* buf, int count, const Datatype& datatype, int dest, 
	   int tag) const;
void Rsend(const void* buf, int count, const Datatype& datatype, int dest, 
	   int tag) const ;
Request Isend(const void* buf, int count, const Datatype& datatype, int dest,
	      int tag) const {
  Request *request = new Request;
  MPIX_CALL( MPI_Isend( (void *)buf, count, 
			(MPI_Datatype)datatype.the_real_dtype, dest, tag, 
			the_real_comm, &(request->the_real_req) ));
  return *request;
}
Request Ibsend(const void* buf, int count, const Datatype& datatype, 
	       int dest, int tag) const ;
Request Issend(const void* buf, int count, const Datatype& datatype, 
	       int dest, int tag) const;
Request Irsend(const void* buf, int count, const Datatype& datatype, 
	       int dest, int tag) const;
Request Irecv(void* buf, int count, const Datatype& datatype, int source, 
	      int tag) const;
bool Iprobe(int source, int tag, Status& status) const;
bool Iprobe(int source, int tag) const;
void Probe(int source, int tag, Status& status) const {
  MPIX_CALL(MPI_Probe( source, tag, the_real_comm, &status.the_real_status ));}
void Probe(int source, int tag) const {
  MPIX_CALL(MPI_Probe( source, tag, the_real_comm, MPI_STATUS_IGNORE ));}
Prequest Send_init(const void* buf, int count, const Datatype& datatype, 
		    int dest, int tag) const;
Prequest Bsend_init(const void* buf, int count, const Datatype& datatype, 
		     int dest, int tag) const;
Prequest Ssend_init(const void* buf, int count, const Datatype& datatype, 
		     int dest, int tag) const;
Prequest Rsend_init(const void* buf, int count, const Datatype& datatype, 
		     int dest, int tag) const;
Prequest Recv_init(void* buf, int count, const Datatype& datatype,
		    int source, int tag) const;
void Sendrecv(const void *sendbuf, int sendcount, const Datatype& sendtype, 
	       int dest, int sendtag, void *recvbuf, int recvcount, 
	       const Datatype& recvtype, int source, int recvtag, 
	       Status& status) const;
void Sendrecv(const void *sendbuf, int sendcount, const Datatype& sendtype, 
	       int dest, int sendtag, 
	       void *recvbuf, int recvcount, const Datatype& recvtype, 
	       int source, int recvtag) const;
void Sendrecv_replace(void* buf, int count, const Datatype& datatype, 
		       int dest, int sendtag, int source, int recvtag, 
		       Status& status) const;
void Sendrecv_replace(void* buf, int count, const Datatype& datatype, 
		       int dest, int sendtag, int source, int recvtag) const;
Group Get_group() const {
  Group *group = new Group;
  MPIX_CALL( MPI_Comm_group( the_real_comm, &group->the_real_group ) );
  return *group;
}
int Get_size() const { int s; 
 MPIX_CALL(MPI_Comm_size( the_real_comm, &s ));
 return s; };
int Get_rank() const { int r; MPI_Comm_rank( the_real_comm, &r ); return r; };
static int Compare(const Comm& comm1, const Comm& comm2);
void Free() { MPIX_CALL(MPI_Comm_free( &the_real_comm )); delete this; }
bool Is_inter() const ;
int Get_topology() const ;
void Abort(int errorcode) {
  MPIX_CALL(MPI_Abort( (MPI_Comm)the_real_comm, errorcode ));}


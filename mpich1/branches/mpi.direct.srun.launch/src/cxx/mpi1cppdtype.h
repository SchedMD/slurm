Datatype Create_contiguous(int count) const {
  Datatype *dtype = new Datatype;
  MPIX_CALL( MPI_Type_contiguous( count, (MPI_Datatype)the_real_dtype,
				  (MPI_Datatype *)&dtype->the_real_dtype ));
  return *dtype;}
Datatype Create_vector(int count, int blocklength, int stride) const;
Datatype Create_indexed(int count, const int array_of_blocklengths[], 
			const int array_of_displacements[]) const;
int Get_size() const {
  int size;
  MPIX_CALL( MPI_Type_size( (MPI_Datatype)the_real_dtype, &size ));
  return size; }
void Commit() {
  MPIX_CALL( MPI_Type_commit( (MPI_Datatype *)&the_real_dtype ) );}
void Free() { MPIX_CALL( MPI_Type_free( (MPI_Datatype *)&the_real_dtype) );
     if (the_real_dtype == MPI_DATATYPE_NULL) { delete this; }}
void Pack(const void* inbuf, int incount, void *outbuf, int outsize, 
	  int& position, const Comm &comm) const ;
void Unpack(const void* inbuf, int insize, void *outbuf, int outcount,
	    int& position, const Comm& comm) const ;
int Pack_size(int incount, const Comm& comm) const ;

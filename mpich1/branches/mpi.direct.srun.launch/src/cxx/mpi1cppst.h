int Get_count(const Datatype& datatype) const {
  int count;
  MPIX_CALL( MPI_Get_count( (MPI_Status*)&the_real_status, 
			    datatype.the_real_dtype, &count ));
  return count; }
bool Is_cancelled() const { int flag;
  MPIX_CALL( MPI_Test_cancelled( (MPI_Status*)&the_real_status, &flag ) );
  return (bool)flag; }
int Get_elements(const Datatype& datatype) const {
  int count;
  MPIX_CALL( MPI_Get_elements( (MPI_Status*)&the_real_status, 
			       datatype.the_real_dtype, &count ));
  return count; }

// Field accessors
int Get_source() const { return the_real_status.MPI_SOURCE; }
void Set_source(int source) { the_real_status.MPI_SOURCE = source; }
int Get_tag() const { return the_real_status.MPI_TAG; }
void Set_tag(int tag) { the_real_status.MPI_TAG = tag; }
int Get_error() const { return the_real_status.MPI_ERROR; }
void Set_error(int error) { the_real_status.MPI_ERROR = error; }

void Wait(Status& status) {
  MPIX_CALL( MPI_Wait( &the_real_req, &status.the_real_status ) ); 
  //  if (the_real_req == MPI_REQUEST_NULL) { delete this; } 
}

void Wait() { MPIX_CALL( MPI_Wait( &the_real_req, MPI_STATUS_IGNORE )); 
//              if (the_real_req == MPI_REQUEST_NULL) { delete this; } 
}
bool Test(Status& status) ;
bool Test() ;
void Free() { MPIX_CALL( MPI_Request_free( &the_real_req ) ); }
static int Waitany(int count, Request array_of_requests[], Status& status);
static int Waitany(int count, Request array_of_requests[]);
static bool Testany(int count, Request array_of_requests[], int& index, 
		    Status& status);
static bool Testany(int count, Request array_of_requests[], int& index);
static void Waitall(int count, Request array_of_requests[], 
		    Status array_of_statuses[]);
static void Waitall(int count, Request array_of_requests[]);
static bool Testall(int count, Request array_of_requests[], 
		    Status array_of_statuses[]);
static bool Testall(int count, Request array_of_requests[]);
static int Waitsome(int incount, Request array_of_requests[], 
		    int array_of_indices[], Status array_of_statuses[]);
static int Waitsome(int incount, Request array_of_requests[], 
		     int array_of_indices[]);
static int Testsome(int incount, Request array_of_requests[], 
		    int array_of_indices[], Status array_of_statuses[]);
static int Testsome(int incount, Request array_of_requests[], 
		     int array_of_indices[]);
void Cancel(void) const {
  MPIX_CALL( MPI_Cancel( (MPI_Request *)&the_real_req ) ); }



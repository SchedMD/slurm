typedef void User_function(const void *invec, void* inoutvec, int len, 
			   const Datatype& datatype);
// An MPI Op is represented by an integer in MPICH.  
void Init(User_function* function, bool commute)
{
  // Need a wrapper to call the C++ function.
  MPIX_CALL( MPI_Op_create( function, (int)commute, &the_real_op ) );
}

void Free()
{
  MPIX_CALL( MPI_Op_free( &the_real_op ) );
  if (the_real_op == MPI_OP_NULL) { delete self; }
}

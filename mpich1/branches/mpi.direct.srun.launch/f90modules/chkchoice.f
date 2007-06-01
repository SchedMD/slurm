	program main
	use mpi
        double precision t1
        integer ierr
        integer add
        call MPI_INIT(ierr)
        call MPI_Address( t1, add, ierr )
        t1 = mpi_wtime()
        print *, t1
        call MPI_FINALIZE(ierr)
	end

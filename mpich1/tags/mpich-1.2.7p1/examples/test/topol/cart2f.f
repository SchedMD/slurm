        program main
        include 'mpif.h'

        integer NUM_DIMS
        parameter (NUM_DIMS=2)

        integer rank, size, i
        integer dims(NUM_DIMS)
        logical periods(NUM_DIMS)
        integer new_coords(NUM_DIMS)
        integer new_new_coords(NUM_DIMS)
        logical reorder
        
        integer comm_cart
        integer ierr

        reorder= .true.
        
        call MPI_INIT(ierr)

        call MPI_COMM_RANK(MPI_COMM_WORLD, rank, ierr)
        call MPI_COMM_SIZE(MPI_COMM_WORLD, size, ierr)

c     Clear dims array and get dims for topology 
        do 100 i=1,NUM_DIMS
                dims(i)=0
                periods(i)=.false.
100     continue
        call MPI_DIMS_CREATE(size, NUM_DIMS, dims, ierr)

c     Make a new communicator with a topology 
        call MPI_CART_CREATE(MPI_COMM_WORLD, 2, dims, periods,
     $          reorder, comm_cart, ierr)

c     Does the mapping from rank to coords work 
        call MPI_CART_COORDS(comm_cart, rank, NUM_DIMS, new_coords,
     $          ierr)

c     2nd call to Cart coords gives us an error - why?    *34*
        call MPI_CART_COORDS(comm_cart, rank, NUM_DIMS, new_new_coords,
     $          ierr)

        call MPI_COMM_FREE(comm_cart, ierr)
c       call Test_Waitforall()
        if (rank .eq. 0) then
c          call MPI_ALLREDUCE( errors, toterrors, 1, MPI_INTEGER,
c       1       MPI_SUM, MPI_COMM_WORLD )
           print *, ' No Errors'
c          print *, ' Done with ', toterrors, ' ERRORS!'
        endif
        call MPI_FINALIZE(ierr)
c        print *, 'cart2f completed, errors=', ierr

        end

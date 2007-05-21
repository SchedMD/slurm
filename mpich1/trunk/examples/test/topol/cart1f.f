        program main
        include 'mpif.h'


        integer NUM_DIMS
        parameter (NUM_DIMS=2)

        integer ierr
        integer errors, toterrors
        integer comm_temp, comm_cart, new_comm
        integer size, rank, i
        logical periods(NUM_DIMS)
        integer dims(NUM_DIMS)
        integer coords(NUM_DIMS)
        integer new_coords(NUM_DIMS)
        logical remain_dims(NUM_DIMS)
        integer newnewrank
        logical reorder
        integer topo_status
        integer ndims
        integer new_rank

        integer source, dest

        errors=0
        call MPI_INIT (ierr)

        call MPI_COMM_RANK( MPI_COMM_WORLD, rank, ierr)
        call MPI_COMM_SIZE (MPI_COMM_WORLD, size, ierr )

c
c    Clear dims array and get dims for topology 
c
        do 100 i=1,NUM_DIMS
                dims(i)=0
                periods(i)= .false.
100     continue
        call MPI_DIMS_CREATE( size, NUM_DIMS, dims, ierr)

c
c     Make a new communicator with a topology 
c
        reorder = .true.
        call MPI_CART_CREATE( MPI_COMM_WORLD, 2, dims, periods,  
     $          reorder, comm_temp, ierr)
        call MPI_COMM_DUP (comm_temp, comm_cart, ierr)

c
c     Determine the status of the new communicator 
c
        call MPI_TOPO_TEST (comm_cart, topo_status, ierr)
        IF (topo_status .ne. MPI_CART) then
           print *, "Topo_status is not MPI_CART"
           errors=errors+1
        ENDIF

c
c     How many dims do we have? 
c
        call MPI_CARTDIM_GET( comm_cart, ndims, ierr)
        if (ndims .ne. NUM_DIMS ) then
           print *, "ndims (", ndims, ") is not NUM_DIMS (", NUMDIMS,
     $          ")" 
           errors = errors+1
        ENDIF

c
c     Get the topology, does it agree with what we put in? 
c
        do 500 i=1,NUM_DIMS
                dims(i)=0
                periods(i)=.false.
500     continue
        call MPI_CART_GET( comm_cart, NUM_DIMS, dims, periods, coords,
     $       ierr) 
c
c     Does the mapping from coords to rank work? 
c
        call MPI_CART_RANK( comm_cart, coords, new_rank, ierr)
        if (new_rank .ne. rank ) then
           print *, "New_rank = ", new_rank, " is not rank (", rank, ")"
           errors=errors+1
        endif

c
c     Does the mapping from rank to coords work 
c
        call MPI_CART_COORDS( comm_cart, rank, NUM_DIMS, new_coords ,
     $       ierr) 
        do 600 i=1,NUM_DIMS
                if (coords(i) .ne. new_coords(i)) then
                   print *, "coords(",i,") = ", coords(i), " not = ",
     $                  new_coords(i) 
                   errors=errors + 1
                endif
600     continue

c
c     Let's shift in each dimension and see how it works!  
c     Because it's late and I'm tired, I'm not making this 
c     automatically test itself.                          
c
        do 700 i=1,NUM_DIMS
           call MPI_CART_SHIFT( comm_cart, (i-1), 1, source, dest, ierr)
c           print *, '[', rank, '] shifting 1 in the ', (i-1), 
c     $                 ' dimension'
c           print *, '[', rank, ']     source = ', source, 
c     $                 ' dest = ', dest
                
700     continue

c
c     Subdivide 
c
        remain_dims(1)=.false.
        do 800 i=2,NUM_DIMS
                remain_dims(i)=.true.
800     continue
        call MPI_CART_SUB( comm_cart, remain_dims, new_comm, ierr)

c
c     Determine the status of the new communicator 
c
        call MPI_TOPO_TEST( new_comm, topo_status, ierr )
        if (topo_status .ne. MPI_CART ) then
           print *, "Topo_status of new comm is not MPI_CART"
           errors=errors+1
        endif

c
c     How many dims do we have? 
c
        call MPI_CARTDIM_GET( new_comm, ndims, ierr)
        if (ndims .ne. NUM_DIMS-1 ) then
           print *, "ndims (", ndims, ") is not NUM_DIMS-1"
           errors = errors+1
        endif

c
c     Get the topology, does it agree with what we put in? 
c
        do 900 i=1,NUM_DIMS-1
                dims(i)=0
                periods(i)=.false.
900     continue
        call MPI_CART_GET( new_comm, ndims, dims, periods, coords, ierr)
    
c
c     Does the mapping from coords to rank work? 
c
        call MPI_COMM_RANK( new_comm, newnewrank, ierr)
        call MPI_CART_RANK( new_comm, coords, new_rank, ierr)
        if (new_rank .ne. newnewrank ) then
           print *, "New rank (", new_rank, ") is not newnewrank"
           errors=errors+1
        endif

c
c     Does the mapping from rank to coords work 
c
        call MPI_CART_COORDS( new_comm, new_rank, NUM_DIMS-1, new_coords
     $       ,  ierr)
        do 1000 i=1,NUM_DIMS-1
                if (coords(i) .ne. new_coords(i)) then
                   print *, "coords(",i,") = ", coords(i),
     $                  " != new_coords (", new_coords(i), ")"
                   errors=errors+1
                endif
1000    continue

c
c     We're at the end 
c
        call MPI_COMM_FREE( new_comm, ierr)
        call MPI_COMM_FREE( comm_temp, ierr)
        call MPI_COMM_FREE( comm_cart, ierr)
        
c       call Test_Waitforall_( )

        call MPI_ALLREDUCE( errors, toterrors, 1, MPI_INTEGER,
     1                      MPI_SUM, MPI_COMM_WORLD, ierr )
        if (rank .eq. 0) then
           if (toterrors .eq. 0) then
              print *, ' No Errors'
           else
              print *, ' Done with ', toterrors, ' ERRORS!'
           endif
        endif
        call MPI_FINALIZE(ierr)
c          print *, '[', rank, '] done with ', errors, ' ERRORS!'

        end

typedef int Copy_attr_function(const MPI::Win& oldwin, int win_keyval, 
			       void* extra_state, void* attribute_val_in, 
			       void* attribute_val_out, bool& flag);
typedef int Delete_attr_function(MPI::Win& win, int win_keyval, 
				 void* attribute_val, void* extra_state);
typedef void Errhandler_fn(Win &, int *, ... );

Group Get_group() const;
bool Test() const;
static Win Create(const void* base, Aint size, int disp_unit, 
		  const Info& info, const Intracomm& comm);
void Accumulate(const void* origin_addr, int origin_count, 
		const Datatype& origin_datatype, int target_rank, 
		Aint target_disp, int target_count,
		const Datatype& target_datatype, const Op& op) const;
void Complete() const;
void Fence(int assert) const { MPIX_CALL( MPI_Win_fence( the_real_win ) ); }
void Free() { MPIX_CALL( MPI_Win_free( &the_real_win ) ); }
void Get(const void *origin_addr, int origin_count, 
	 const Datatype& origin_datatype, int target_rank, Aint target_disp, 
	 int target_count, const Datatype& target_datatype) const;
void Lock(int lock_type, int rank, int assert) const {
  MPIX_CALL( MPI_Win_lock( the_real_win, lock_type, rank assert ) ); }
void Post(const Group& group, int assert) const;
void Put(const void* origin_addr, int origin_count, 
	 const Datatype& origin_datatype, int  target_rank, Aint target_disp, 
	 int target_count, const  Datatype& target_datatype) const;
void Start(const Group& group, int assert) const;
void Unlock(int rank) const { 
  MPIX_CALL( MPI_Win_lock( the_real_win, rank ) ); }
void Wait() const;
bool Get_attr(const Win& win, int win_keyval, void* attribute_val) const;
static int Create_keyval(Copy_attr_function* win_copy_attr_fn, 
			 Delete_attr_function* win_delete_attr_fn, 
			 void* extra_state);
static void Free_keyval(int& win_keyval);
void Call_errhandler(int errorcode) const;
void Delete_attr(int win_keyval);
void Get_name(char* win_name, int& resultlen) const;
void Set_attr(int win_keyval, const void* attribute_val);
void Set_name(const char* win_name);
Errhandler Get_errhandler() const;
static Errhandler Create_errhandler(Errhandler_fn* function);
void Set_errhandler(const Errhandler& errhandler);

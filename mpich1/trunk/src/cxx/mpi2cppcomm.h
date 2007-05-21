typedef int Copy_attr_function(const MPI::Comm& oldcomm, int comm_keyval, void* extra_state, void* attribute_val_in, void* attribute_val_out, bool& flag);

typedef int Delete_attr_function(MPI::Comm& comm, int comm_keyval, void* attribute_val, void* extra_state);

typedef void Errhandler_fn(MPI::Comm &, int *, ... );

void Disconnect(void);
bool Get_attr(int comm_keyval, void* attribute_val) const;
static int Create_keyval(Copy_attr_function* comm_copy_attr_fn, Delete_attr_function* comm_delete_attr_fn, void* extra_state);
static void Free_keyval(int& comm_keyval);
void Call_errhandler(int errorcode) const;
void Delete_attr(int comm_keyval);
void Get_name(char* comm_name, int& resultlen) const;
void Set_attr(int comm_keyval, const void* attribute_val) const;
void Set_name(const char* comm_name);
Errhandler Get_errhandler(void) const;
static Errhandler Create_errhandler(Errhandler_fn* function);
void Set_errhandler(const Errhandler& errhandler);

void Attach_buffer(void* buffer, int size);
int Detach_buffer(void*& buffer);
void Compute_dims(int nnodes, int ndims, int dims[]);
void Get_processor_name(char* name, int& resultlen);
void Get_error_string(int errorcode, char* name, int& resultlen);
int Get_error_class(int errorcode);
//inline double Wtime() { return MPI_Wtime(); }
//inline double Wtick() { return MPI_Wtick(); }
void Init(int& argc, char**& argv);
void Init();
void Finalize(void);
bool Is_initialized(void);
void Pcontrol(const int level, ...);
void Get_version(int& version, int& subversion);

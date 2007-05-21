MPI::Intercomm Accept(const char* port_name, const MPI::Info& info, int root) const;
MPI::Intercomm Connect(const char* port_name, const MPI::Info& info, int root) const;
MPI::Intercomm Spawn(const char* command, const char* argv[], int maxprocs, const MPI::Info& info, int root) const;
MPI::Intercomm Spawn(const char* command, const char* argv[], int maxprocs, const MPI::Info& info, int root, int array_of_errcodes[]) const;
MPI::Intercomm Spawn_multiple(int count, const char* array_of_commands[], const char** array_of_argv[], const int array_of_maxprocs[], const MPI::Info array_of_info[], int root);
MPI::Intercomm Spawn_multiple(int count, const char* array_of_commands[], const char** array_of_argv[], const int array_of_maxprocs[], const MPI::Info array_of_info[], int root, int array_of_errcodes[]);
void Alltoallw(const void* sendbuf, const int sendcounts[], const int sdispls[], const MPI::Datatype sendtypes[], void* recvbuf, const int recvcounts[], const int rdispls[], const MPI::Datatype recvtypes[]) const;
void Exscan(const void* sendbuf, void* recvbuf, int count, const MPI::Datatype& datatype, const MPI::Op& op) const;

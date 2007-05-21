 
// C++ bindings
 
// Process Creation and Management
void Close_port(const char* port_name);
void Lookup_name(const char* service_name, const Info& info, char* port_name);
void Open_port(const Info& info, char* port_name);
void Publish_name(const char* service_name, const Info& info, 
		  const char* port_name);
void Unpublish_name(const char* service_name, const Info& info, 
		    const char* port_name);
 
// One-Sided Communications
 
// Extended Collective Operations
 
// External Interfaces
bool Is_thread_main();
int Add_error_class();
int Add_error_code(int errorclass);
int Init_thread(int required);
int Init_thread(int& argc, char**& argv, int required);
int Query_thread();
//static Grequest Grequest::Start(const Grequest::Query_function query_fn, const Grequest::Free_function free_fn, const Grequest::Cancel_function cancel_fn, void *extra_state);
void Add_error_string(int errorcode, const char* string);
//void Grequest::Complete();
 
// Miscellany
Aint Get_address(void* location);
bool Is_finalized();
void Free_mem(void *base);
void* Alloc_mem(Aint size, const Info& info);
 
// I/O
typedef void Datarep_conversion_function(void* userbuf, Datatype& datatype,
					 int count, void* filebuf,
					 Offset position, void* extra_state);
typedef void Datarep_extent_function(const Datatype& datatype, 
				     Aint& file_extent, void* extra_state);

void Register_datarep(const char* datarep, 
		      Datarep_conversion_function* read_conversion_fn, 
		      Datarep_conversion_function* write_conversion_fn, 
		      Datarep_extent_function* dtype_file_extent_fn, 
		      void* extra_state);
 
// C++ functions
//typedef int Grequest::Cancel_function(void* extra_state, bool complete);
//typedef int Grequest::Free_function(void* extra_state);
//typedef int Grequest::Query_function(void* extra_state, Status& status);


typedef int Delete_attr_function(Datatype& type, int type_keyval, 
				 void* attribute_val, void* extra_state); 
typedef int Copy_attr_function(const Datatype& oldtype, int type_keyval, 
			       void* extra_state, 
			       const void* attribute_val_in, 
			       void* attribute_val_out, bool& flag);

Datatype Dup() const;
bool Get_attr(int type_keyval, void* attribute_val) const;
static int Create_keyval(Copy_attr_function* type_copy_attr_fn, 
			 Delete_attr_function* type_delete_attr_fn, 
			 void* extra_state);
static void Free_keyval(int& type_keyval);
void Delete_attr(int type_keyval);
void Get_contents(int max_integers, int max_addresses, int max_datatypes, 
		  int array_of_integers[], Aint array_of_addresses[], 
		  Datatype array_of_datatypes[]) const;
void Get_envelope(int& num_integers, int& num_addresses, int& num_datatypes, 
		  int& combiner) const;
void Get_name(char* type_name, int& resultlen) const;
void Set_attr(int type_keyval, const void* attribute_val);
void Set_name(const char* type_name) {
//  MPIX_CALL( MPI_Type_set_name( the_real_dtype, type_name ) ); 
}
Aint Pack_external_size(const char* datarep, int incount) const;
Datatype Create_darray(int size, int rank, int ndims, 
		       const int array_of_gsizes[], 
		       const int array_of_distribs[], 
		       const int array_of_dargs[], 
		       const int array_of_psizes[], int order) const;
Datatype Create_hindexed(int count, const int array_of_blocklengths[], 
			 const Aint array_of_displacements[]) const;
Datatype Create_hvector(int count, int blocklength, Aint stride) const;
Datatype Create_indexed_block( int count, int blocklength, 
			       const int array_of_displacements[]) const;
Datatype Create_subarray(int ndims, const int array_of_sizes[], 
			 const int array_of_subsizes[], 
			 const int array_of_starts[], int order) const;
Datatype Resized(const Aint lb, const Aint extent) const;
static Datatype Create_struct(int count, 
			      const int array_of_blocklengths[], 
			      const Aint array_of_displacements[], 
			      const Datatype array_of_types[]);
void Get_extent(Aint& lb, Aint& extent) const;
void Get_true_extent(Aint& true_lb, Aint& true_extent) const;
void Pack_external(const char* datarep, const void* inbuf, int incount, 
		   void* outbuf, Aint outsize, Aint& position) const;
void Unpack_external(const char* datarep, const void* inbuf, Aint insize, 
		     Aint& position, void* outbuf, int outcount) const ;

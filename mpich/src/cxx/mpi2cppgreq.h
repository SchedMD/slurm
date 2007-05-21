typedef int Cancel_function(void* extra_state, bool complete);
typedef int Free_function(void* extra_state);
typedef int Query_function(void* extra_state, MPI::Status& status);

static MPI::Grequest Start(const Query_function query_fn, const Free_function free_fn, const Cancel_function cancel_fn, void *extra_state);
void Complete();

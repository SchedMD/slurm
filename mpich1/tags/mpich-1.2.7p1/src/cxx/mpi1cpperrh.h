typedef void Handler_function( Comm *, int *, ... );
void Init(const Handler_function* function);
void Free();

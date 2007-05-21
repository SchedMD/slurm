MPI::Info Dup() const;
bool Get(const char* key, int valuelen, char* value) const;
bool Get_valuelen(const char* key, int& valuelen) const;
int Get_nkeys() const;
static MPI::Info Create();
void Delete(const char* key);
void Free();
void Get_nthkey(int n, char* key) const;
void Set(const char* key, const char* value);

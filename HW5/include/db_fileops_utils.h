unsigned long long fnv1a(const char* str, int len);
unsigned long long checksum_content(const char* path, size_t file_size);
int build_fileops_entry(const char* path, struct db_fileops_entry* entry);
int append_fileops_entry(int fd_db, struct db_fileops_entry* entry);
int compare_fileops_entries(int fd_db, struct db_fileops_entry* entry, unsigned int db_entry_offset);
int validate_directory(const char* path);
int open_database(const char* path, int magic);
int close_database(int fd_db, int use_explicit_path);
long long get_header_field(int fd_db, int field);
unsigned int next_entry(int fd_db, unsigned int entry_offset, int next_is_regular);
int write_to_db(int fd_db, char entry_buffer[], unsigned int entry_size, int entry_bucket);
int set_header_field(int fd_db, int field, unsigned long long value);
int initialize_snapshot(int fd_db, int magic);

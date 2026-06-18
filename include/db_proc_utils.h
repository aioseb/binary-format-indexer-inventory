int build_proc_entry(int pid, struct db_proc_entry* entry);
int append_proc_entry(int fd_db, struct db_proc_entry* entry);
unsigned int next_proc_entry(int fd_db, unsigned int entry_offset, int next_is_regular);
int compare_proc_entries(int fd_db, struct db_proc_entry* entry, unsigned int db_entry_offset);
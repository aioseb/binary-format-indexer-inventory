#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include "../include/db_utils.h"
#include "../include/db_types.h"
#include "../include/db_fileops_utils.h"
#include "../include/db_proc_utils.h"

#define THRESHOLD 1024	// Pragul in KB pentru diferenta absoluta dintre RSS-uri pentru procese

// Verifica daca un fileops_entry difera prin campuri 
// Returneaza 0 pentru egalitate, 1 pentru diferenta si -1 pentru erori
// Se compara: mtime, checksum, size_bytes si type
int compare_fileops_fields(int fd_db, struct db_fileops_entry* entry, unsigned int db_entry_offset){
    db_entry_offset += HEADER_SIZE;
    
    int db_path_len;
    int read_bytes = pread(fd_db, &db_path_len, sizeof(uint32_t), db_entry_offset + IDX_PATH_LEN_OFFSET);

    if(read_bytes != sizeof(uint32_t)){
        if(read_bytes == 0){
            // s-a citit de la EOF
            return 1;
        }
        // printf("%d\n", read_bytes);
        fprintf(stderr, "Eroare: citire IDX_PATH_LEN la 0x%X\n", db_entry_offset);
        return -1;
    }

	unsigned int offset_after_path = IDX_ABS_PATH_OFFSET + db_path_len + TYPE_LEN_MAX + db_entry_offset;
	uint32_t db_mtime;
	uint64_t db_checksum;
	uint64_t db_size_bytes;
	char db_type[TYPE_LEN_MAX];

	if(sizeof(uint32_t) != pread(fd_db, &db_mtime, sizeof(uint32_t), offset_after_path + 2 * sizeof(uint64_t))){
		fprintf(stderr, "Eroare: citire IDX_MTIME in compare_fileops_fields\n");
		return -1;
	}
	if(entry->mtime != db_mtime){
		return 1;
	}

	if(sizeof(uint64_t) != pread(fd_db, &db_checksum, sizeof(uint64_t), offset_after_path)){
		fprintf(stderr, "Eroare: citire IDX_CHECKSUM in compare_fileops_fields\n");
		return -1;
	}
	if(entry->checksum != db_checksum){
		return 1;
	}

	if(sizeof(uint64_t) != pread(fd_db, &db_size_bytes, sizeof(uint64_t), offset_after_path + sizeof(uint64_t))){
		fprintf(stderr, "Eroare: citire IDX_SIZE_BYTES in compare_fileops_fields\n");
		return -1;
	}
	if(entry->size_bytes != db_size_bytes){
		return 1;
	}

	if(TYPE_LEN_MAX != pread(fd_db, db_type, TYPE_LEN_MAX, offset_after_path - TYPE_LEN_MAX)){
		fprintf(stderr, "Eroare: citire IDX_FILE_TYPE in compare_fileops_fields\n");
		return -1;
	}
	if(strcmp(entry->type, db_type) != 0){
		return 1;
	}

	return 0;
}

// Verifica daca un proc_entry difera prin campuri 
// Returneaza 0 pentru egalitate, 1 pentru diferenta si -1 pentru erori
// Se considera ca fiind "modificat semnificativ" diferenta dintre: comm, cmdline, rss threshold 
int compare_proc_fields(int fd_db, struct db_proc_entry* entry, unsigned int db_entry_offset){
	db_entry_offset += HEADER_SIZE;
    
	uint32_t db_comm_size;
	uint32_t db_cmdline_size;
	uint32_t db_rss_src_size;
	uint64_t db_rss;
	char* db_cmdline;
	char* db_comm;

	if(sizeof(uint32_t) != pread(fd_db, &db_comm_size, sizeof(uint32_t), PROC_COMM_SIZE_OFFSET + db_entry_offset)){
		fprintf(stderr, "Eroare: citire PROC_COMM_SIZE in compare_fileops_fields\n");
		return -1;
	}
	if(entry->comm_size != db_comm_size){
		return 1;
	}

	if(sizeof(uint32_t) != pread(fd_db, &db_cmdline_size, sizeof(uint32_t), PROC_COMM_SIZE_OFFSET + db_comm_size + sizeof(uint32_t) + db_entry_offset)){
		fprintf(stderr, "Eroare: citire PROC_CMDLINE_SIZE in compare_fileops_fields\n");
		return -1;
	}
	if(entry->cmdline_size != db_cmdline_size){
		return 1;
	}
	
	if(sizeof(uint32_t) != pread(fd_db, &db_rss_src_size, sizeof(uint32_t), PROC_COMM_SIZE_OFFSET + 
								 db_comm_size + db_cmdline_size + 2 * sizeof(uint32_t) + db_entry_offset)){
		fprintf(stderr, "Eroare: citire PROC_RSS_SRC_SIZE in compare_fileops_fields\n");
		return -1;							
	}

	if(sizeof(uint64_t) != pread(fd_db, &db_rss, sizeof(uint64_t), PROC_COMM_SIZE_OFFSET + 
								 db_comm_size + db_cmdline_size + db_rss_src_size + 3 * sizeof(uint32_t) + db_entry_offset)){
		fprintf(stderr, "Eroare: citire PROC_RSS_SRC_SIZE in compare_fileops_fields\n");
		return -1;							
	}

	uint64_t diff = (entry->rss > db_rss) ? (entry->rss - db_rss) : (db_rss - entry->rss);
	if(diff > THRESHOLD * 1024){
		return 1;	// S-a trecut de pragul de THRESHOLD
	}
	
	db_comm = malloc(db_comm_size);
	if(db_comm_size != pread(fd_db, db_comm, db_comm_size, PROC_COMM_SIZE_OFFSET + sizeof(uint32_t) + db_entry_offset)){
		fprintf(stderr, "Eroare: citire PROC_COMM in compare_fileops_fields\n");
		free(db_comm);
		return -1;
	}
	if(strcmp(entry->comm, db_comm) != 0){
		free(db_comm);
		return 1;
	}
	free(db_comm);
	
	db_cmdline = malloc(db_cmdline_size);
	if(db_cmdline_size != pread(fd_db, db_cmdline, db_cmdline_size, PROC_COMM_SIZE_OFFSET + db_comm_size + 2 * sizeof(uint32_t) + db_entry_offset)){
		fprintf(stderr, "Eroare: citire PROC_CMDLINE in compare_fileops_fields\n");
		free(db_cmdline);
		return -1;
	}
	if(strcmp(entry->cmdline, db_cmdline) != 0){
		free(db_cmdline);
		return 1;
	}
	free(db_cmdline);

	return 0;
}


// Verifica daca ambele baze de date sunt de acelasi tip si au aceeasi versiune
// Returneaza 0 in caz de succes, sau 1 daca snapshot-urile sunt incompatibile
// Se returneaza -1 in caz de eroare
int check_snapshots(const char* old_db, const char* new_db){
	// Citeste informatiile din vechiul snapshot
	int fd_old = open(old_db, O_RDONLY);
	if(fd_old == -1){
		perror("Eroare: open old_db in check_snapshots");
		return -1;
	}

	int magic_old = get_header_field(fd_old, MAGIC);
	int version_old = get_header_field(fd_old, VERSION);
	int id_old = get_header_field(fd_old, SNAPSHOT_ID);
	if(magic_old == -1 || version_old == -1 || id_old == -1){
		return -1;
	}

	close(fd_old);

	// Citeste informatiile din noul snapshot
	int fd_new = open(new_db, O_RDONLY);
	if(fd_new == -1){
		perror("Eroare: open new_db in check_snapshots");
		return -1;
	}

	int magic_new = get_header_field(fd_new, MAGIC);
	int version_new = get_header_field(fd_new, VERSION);
	int id_new = get_header_field(fd_new, SNAPSHOT_ID);
	if(magic_new == -1 || version_new == -1 || id_new == -1){
		return -1;
	}

	close(fd_new);

	// Compara informatiile din header-ele snapshot-urilor
	if(magic_old != magic_new || version_old != version_new){
		return 1;
	}

	return 0;
}

// Verifica statusul unui entry dat prin offset-ul din "vechiul snapshot" in "noul snapshot" de tipul dat
// Se returneaza:
// 0 daca se gaseste
// 1 daca entry-ul a fost modificat
// 2 daca entry-ul nu se gaseste
// 3 daca suntem la EOF
// -1 in caz de eroare
// buffer-ul in care a fost copiat entry-ul cu campurile relevante
int lookup_entry(unsigned int entry_offset, int fd_old, int fd_new, int magic, unsigned char entry_buffer[]){
	entry_offset += HEADER_SIZE;
	int entry_size;

	size_t bytes_read = pread(fd_old, &entry_size, sizeof(uint32_t), entry_offset);

	if(-1 == bytes_read){
		fprintf(stderr, "Eroare: citire entry_size in lookup_entry\n");
		return -1;
	}
	else if(0 == bytes_read){
		return 3;	// Suntem la EOF
	}

	if(entry_size > MAX_ENTRY_SIZE){
		fprintf(stderr, "Eroare: entry_size mai mare decat MAX_ENTRY_SIZE in lookup_entry\n");
		return -1;
	}

	// Bufferul in care se pune informatiile despre entry-ul respectiv
	ssize_t read_bytes = pread(fd_old, entry_buffer, entry_size, entry_offset);
	if(entry_size != read_bytes){
		if(read_bytes == 0){
			// S-a ajuns la EOF
			return 3;
		}
		else {
			fprintf(stderr, "Eroare: citire partiala sau incorecta in entry_buffer in lookup_entry\n");
			return -1;
		}
	}

	// Calculeaza BUCKET-ul in care se afla in functie de tipul formatului
	int entry_bucket;

	struct db_fileops_entry index;
	struct db_proc_entry proc;

	// Copiaza doar campurile relevante
	if(magic == FILEOPS_MAGIC_ID){
		index.entry_size = entry_size;
		memcpy(&index.path_len, entry_buffer + IDX_PATH_LEN_OFFSET, sizeof(uint32_t));
		index.absolute_path = (char *)(entry_buffer + IDX_ABS_PATH_OFFSET);
		memcpy(index.type, entry_buffer + IDX_ABS_PATH_OFFSET + index.path_len, TYPE_LEN_MAX);
		memcpy(&index.checksum, entry_buffer + IDX_ABS_PATH_OFFSET + index.path_len + TYPE_LEN_MAX,  sizeof(uint64_t));
		memcpy(&index.size_bytes, entry_buffer + IDX_ABS_PATH_OFFSET + index.path_len + TYPE_LEN_MAX + sizeof(uint64_t), sizeof(uint64_t));
		memcpy(&index.mtime, entry_buffer + IDX_ABS_PATH_OFFSET + index.path_len + TYPE_LEN_MAX + 2 * sizeof(uint64_t), sizeof(uint32_t));
		memcpy(&index.hash, entry_buffer + IDX_ABS_PATH_OFFSET + index.path_len + TYPE_LEN_MAX + sizeof(uint32_t) + 2 * sizeof(uint64_t), sizeof(uint64_t));

		entry_bucket = index.hash % BUCKET_COUNT;
		entry_size = index.entry_size;
	}
	else if(magic == PROC_MAGIC_ID){
		proc.entry_size = entry_size;
		memcpy(&proc.pid, entry_buffer + PROC_PID_OFFSET, sizeof(uint32_t));
		memcpy(&proc.comm_size, entry_buffer + PROC_COMM_SIZE_OFFSET, sizeof(uint32_t));
		proc.comm = (char *)(entry_buffer + PROC_COMM_SIZE_OFFSET + sizeof(uint32_t));
		memcpy(&proc.cmdline_size, entry_buffer + PROC_COMM_SIZE_OFFSET + sizeof(uint32_t) + proc.comm_size, sizeof(uint32_t));
		proc.cmdline = (char *)(entry_buffer + PROC_COMM_SIZE_OFFSET + 2 * sizeof(uint32_t) + proc.comm_size);
		memcpy(&proc.rss_src_size, entry_buffer + PROC_COMM_SIZE_OFFSET + 2 * sizeof(uint32_t) + proc.comm_size + proc.cmdline_size, sizeof(uint32_t));
		memcpy(&proc.rss, entry_buffer + PROC_COMM_SIZE_OFFSET + 3 * sizeof(uint32_t) + proc.comm_size + proc.cmdline_size + proc.rss_src_size, sizeof(uint64_t));

		entry_bucket = proc.pid % BUCKET_COUNT;
		entry_size = proc.entry_size;
	}
	else{
		fprintf(stderr, "Eroare: format necunoscut in lookup_entry\n");
		return -1;
	}

	// Parcurge si compara entry-ul curent din old_db cu entry-urile din new_db
	long long other_offset = get_header_field(fd_new, BUCKET_HEAD + entry_bucket);
	if(other_offset == EMPTY_BUCKET){
		return 2;	// Nu s-a gasit
	}

	other_offset -= HEADER_SIZE;

	do{
		if(other_offset == 1){
			fprintf(stderr, "Eroare: parcurgere in lookup_entry\n");
			return -1;
		}
		int ret;

		// Comparam mai intai dupa cheia primara, dupa care dupa alte campuri
		if(magic == FILEOPS_MAGIC_ID){
			// Mentiune: symlink target-ul se compara implicit, acesta facand parte din IDX_ABS_PATH
			ret = compare_fileops_entries(fd_new, &index, other_offset);

			if(ret == -1){
				fprintf(stderr, "Eroare: compare_fileops_entries in lookup_entry");
				return -1;
			}
			else if(ret == 0){
				// S-au gasit chei primare identice. Se face comparatia dupa campuri
				ret = compare_fileops_fields(fd_new, &index, other_offset);

				if(ret == 0){
					return 0;	// Entry-ul a fost gasit
				}
				else if(ret == 1){
					return 1;	// Entry-ul a fost modificat
				}

				fprintf(stderr, "Eroare: compare_fileops_fields in lookup_entry\n");
				return -1;		// Eroare
			}
		}
		else if(magic == PROC_MAGIC_ID){
			ret = compare_proc_entries(fd_new, &proc, other_offset);

			if(ret == -1){
				fprintf(stderr, "Eroare: compare_proc_entries in lookup_entry");
				return -1;
			}
			else if(ret == 0){
				// S-au gasit chei primare identice. Se face comparatia dupa campuri
				ret = compare_proc_fields(fd_new, &proc, other_offset);

				if(ret == 0){
					return 0;	// Entry-ul a fost gasit
				}
				else if(ret == 1){
					return 1;	// Entry-ul a fost modificat
				}

				fprintf(stderr, "Eroare: compare_fileops_fields in lookup_entry\n");
				return -1;		// Eroare
			}
		}

		other_offset = next_entry(fd_new, other_offset, NEXT_IS_BUCKET);
	} while(other_offset != 0);

	// Entry-ul nu a fost gasit
	return 2;
}

int main(int argc, char** argv){
	if (argc <= 6){
        fprintf(stderr, "Folosire: %s --old <db1> --new <db2> --out <path>\n", argv[0]);
        exit(1);
    }

	const char* old_db = NULL;
	const char* new_db = NULL;
	const char* path_diff = NULL;

    // Stabileste directorul sursa si destinatia bazei de date binare
    for(int i = 1; i < argc - 1; i++){
        const char* opt = argv[i];

        if(strcmp(opt, "--old") == 0){
            old_db = argv[i + 1];
            i++;    // Sari peste numele directorului
        }

        if(strcmp(opt, "--new") == 0){
            new_db = argv[i + 1];
            i++;    // Sari peste numele destinatiei
        }

		if(strcmp(opt, "--out") == 0){
			path_diff = argv[i + 1];
			i++;
		}
    }

	int ret_snapshots = check_snapshots(old_db, new_db);
	if(ret_snapshots == -1){
		fprintf(stderr, "Eroare: check_snapshots\n");
		return 1;
	}
	else if(ret_snapshots == 1){
		printf("Fisiere .db incompatibile\n");
		return 0;
	}

	int fd_old = open(old_db, O_RDONLY);
	if(fd_old == -1){
		perror("Eroare: open old snapshot in main_db_diff.c");
		return 1;
	}

	int fd_new = open(new_db, O_RDONLY);
	if(fd_new == -1){
		perror("Eroare: open new snapshot in main_db_diff.c");
		return 1;
	}

	int fd_out = open(path_diff, O_WRONLY | O_APPEND | O_CREAT | O_TRUNC, 0644);
	if(fd_out == -1){
		perror("Eroare: open path diff in main_db_diff.c");
		return 1;
	}

	int magic = get_header_field(fd_old, MAGIC);
	unsigned char entry_buffer[MAX_ENTRY_SIZE];

	// Listeaza inregistrarile sterse si cele modificate
	unsigned int entry_offset = 0;
	do{
		int ret = lookup_entry(entry_offset, fd_old, fd_new, magic, entry_buffer);
		
		if(ret == 1){
			// Modificat
			dprintf(fd_out, "Modificat: ");
		}
		else if(ret == 2){
			// Sters
			dprintf(fd_out, "Sters: ");
		}

		if(ret == 1 || ret == 2){
			if(magic == FILEOPS_MAGIC_ID){
				dprintf(fd_out, "%s\n", entry_buffer + IDX_ABS_PATH_OFFSET);
			}
			else if(magic == PROC_MAGIC_ID){
				int pid;
				memcpy(&pid, entry_buffer + PROC_PID_OFFSET, sizeof(int));
				dprintf(fd_out, "PID = %d\n", pid);
			}
		}
		
		entry_offset = next_entry(fd_old, entry_offset, NEXT_IS_REGULAR);
		if(entry_offset == 1 || ret == -1){
			fprintf(stderr, "Eroare: parcurgere baza de date in main_db_diff\n");
			return -1;
		}
	} while(entry_offset != 0);

	// Listeaza inregistrarile noi
	entry_offset = 0;
	do{
		int ret = lookup_entry(entry_offset, fd_new, fd_old, magic, entry_buffer);
		
		if(ret == 2){
			// Creat
			dprintf(fd_out, "Creat: ");
			
			if(magic == FILEOPS_MAGIC_ID){
				dprintf(fd_out, "%s\n", entry_buffer + IDX_ABS_PATH_OFFSET);
			}
			else if(magic == PROC_MAGIC_ID){
				int pid;
				memcpy(&pid, entry_buffer + PROC_PID_OFFSET, sizeof(int));
				dprintf(fd_out, "PID = %d\n", pid);
			}
		}
		
		entry_offset = next_entry(fd_new, entry_offset, NEXT_IS_REGULAR);
		if(entry_offset == 1 || ret == -1){
			fprintf(stderr, "Eroare: parcurgere baza de date in main_db_diff\n");
			return -1;
		}
	} while(entry_offset != 0);

	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include "../include/db_types.h"
#include "../include/db_utils.h"
#include "../include/db_proc_utils.h"

#define MAX_PID_COUNT 4096

// Construieste baza de date pentru proceses
int build_database(int fd_db, int pids[], int pids_count){
    int ret_build_proc = 0;
    unsigned long long appended_records = 0;
    struct db_proc_entry proc_entry;

    for(int i = 0; i < pids_count; i++){
        int pid = pids[i];
        ret_build_proc = build_proc_entry(pid, &proc_entry);

        if(ret_build_proc == -1){
            fprintf(stderr, "Eroare: build_proc_entry in build_database\n");
            return 2;
        }
        else if(ret_build_proc == 0){
            int ret_append = append_proc_entry(fd_db, &proc_entry);
            free(proc_entry.comm);
            free(proc_entry.cmdline);
                        
            if(ret_append == -1){
                fprintf(stderr, "Eroare: append_proc_entry in build_database\n");
                return 2;
            }
            else if(ret_append == 0){
                appended_records++;
            }
            else{
                // Ignora inregistrarile deja adaugate
            }
        }
        else if(ret_build_proc == 1){
            // Ignora procesele disparute
        }
    }

    // Actualizarea numarului de inregistrari
    struct flock hl;

    hl.l_type = F_WRLCK;
    hl.l_whence = SEEK_SET;
    hl.l_start = RECORD_COUNT_OFFSET;
    hl.l_len = 4;

    // Punem lacat peste nr. de inregistrari
    if(fcntl(fd_db, F_SETLKW, &hl) == -1){
        perror("Eroare: lock in db_util.c");
        return -1;
    }

    int record_count = get_header_field(fd_db, RECORD_COUNT);
    set_header_field(fd_db, RECORD_COUNT, record_count + appended_records);
    
    // Luam lacatul de pe nr. de inregistrari
    hl.l_type = F_UNLCK;
    if(fcntl(fd_db, F_SETLK, &hl) == -1){
        perror("Eroare: unlock in db_util.c");
        return -1;
    }
    hl.l_type = F_WRLCK;

    return 0;
}

// Construieste un buffer cu procesele curente la initializare
// Returneaza numarul de procese inserate
int build_buffer(int pids[]){
    const char* proc_path = "/proc";
    DIR* dir_stream;
    struct dirent* ent;

    int pids_count = 0;
    char* end;

    if(NULL != (dir_stream = opendir(proc_path))){
        while(pids_count < MAX_PID_COUNT && NULL != (ent = readdir(dir_stream))){
            // Verifica daca fisierul reprezinta un proces
            uint32_t pid = strtol(ent->d_name, &end, 10);

            if(*end == '\0'){
                pids[pids_count] = pid;
                pids_count++;
            }
            else{
                // Ignore fisierele care nu sunt procese
            }
        }
    }
    else{
        perror("Eroare: deschidere proc/");
        return -1;
    }

    return pids_count;
}

int main(int argc, char** argv){
    const char* dest = "./data/proc.db";
    int use_explicit_path = 0;

    // Stabileste directorul destinatie a bazei de date binare
    for(int i = 1; i < argc - 1; i++){
        const char* opt = argv[i];

        if(strcmp(opt, "--db") == 0){
            dest = argv[i + 1];
            i++;    // Sari peste numele destinatiei
            use_explicit_path = 1;
        }
 
    }

    int fd_db = open_database(dest, PROC_MAGIC_ID);
    if(fd_db == -1){
        fprintf(stderr, "Eroare: deschidere db in main_proc_snapshot\n");
        exit(1);
    }

    int pids_buffer[MAX_PID_COUNT];
    int pids_count = build_buffer(pids_buffer);

    if(0 != build_database(fd_db, pids_buffer, pids_count)){
        fprintf(stderr, "Eroare: build_database\n");
        close_database(fd_db, use_explicit_path);
        exit(1);
    }

    close_database(fd_db, use_explicit_path);
}



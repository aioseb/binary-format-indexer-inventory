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
#include "../include/db_fileops_utils.h"

// Construieste in maniera recursiva baza de date in <dest>
int build_database(int fd_db, const char* dir){
    DIR* dir_stream;
    struct dirent *ent;
    struct stat st;
    struct db_fileops_entry entry;
    entry.absolute_path = NULL;
    char resolved_path[PATH_MAX + 1]; 

    unsigned long long appended_records = 0;
    int ret_build_entry = 0;    // Flag-uri folosite pentru a verifica functionarea scrierii in baza de date
    int ret_append_entry = 0;

    if(NULL != (dir_stream = opendir(dir))){
        while(NULL != (ent = readdir(dir_stream))){
            if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0){ // Ignora . si ..
                continue;
            }

            // Construieste calea absoluta in "resolved_path"
            // Verifica daca scriem mai mult decat trebuie
            if(snprintf(resolved_path, sizeof(resolved_path), "%s/%s", dir, ent->d_name) >= sizeof(resolved_path)){
                fprintf(stderr, "Eroare: calea este prea lunga in build_database!\n");
                return 3;
            };

            // Initializeaza intrarea
            if(-1 == (ret_build_entry = build_fileops_entry(resolved_path, &entry))){
                fprintf(stderr, "Eroare: build_entry in build_database!\n");
                return ret_build_entry;
            };
            
            // Introduce intrarea in baza de date
            if(-1 == (ret_append_entry = append_fileops_entry(fd_db, &entry))){
                free(entry.absolute_path);
                fprintf(stderr, "Eroare: append_entry in build_database!\n");
                return ret_append_entry;
            };

            free(entry.absolute_path);
            if(ret_append_entry == 0){
                appended_records++;
            }
            
            if(0 != lstat(resolved_path, &st)){
                perror("Eroare: lstat in build_database");
                exit(1);
            };
            if(S_ISDIR(st.st_mode)){
                int ret_build_db = build_database(fd_db, resolved_path); // Parcurge recursiv directorul nou gasit
                if(ret_build_db != 0){
                    closedir(dir_stream);
                    return ret_build_db;
                }
            }
        }

        closedir(dir_stream);   
    }
    else{
        perror("Eroare: opendir in build_database");
        return 3;
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

int main(int argc, char** argv){
    if (argc <= 2){
        fprintf(stderr, "Folosire: %s --root <dir> [--db <path>]\n", argv[0]);
        exit(1);
    }

    const char* dir = NULL;
    const char* dest = "./data/index.db";
    int use_explicit_path = 0;

    // Stabileste directorul sursa si destinatia bazei de date binare
    for(int i = 1; i < argc - 1; i++){
        const char* opt = argv[i];

        if(strcmp(opt, "--root") == 0){
            dir = argv[i + 1];
            i++;    // Sari peste numele directorului
        }

        if(strcmp(opt, "--db") == 0){
            dest = argv[i + 1];
            i++;    // Sari peste numele destinatiei
            use_explicit_path = 1;
        }
    }

    if(0 != validate_directory(dir)){
        fprintf(stderr, "Eroare: validare director\n");
        exit(1);
    };

    int fd_db = open_database(dest, FILEOPS_MAGIC_ID);
    if(fd_db == -1){
        fprintf(stderr, "Eroare: deschidere db in main_fileops_indexer\n");
        exit(1);
    };

    char* abs_path_dir = realpath(dir, NULL);
    if(!abs_path_dir){
        perror("Eroare: realpath in main");
        close_database(fd_db, use_explicit_path);
        exit(1);
    }

    if(0 != build_database(fd_db, abs_path_dir)){
        fprintf(stderr, "Eroare: build_database\n");
        close_database(fd_db, use_explicit_path);
        exit(1);
    }

    close_database(fd_db, use_explicit_path);
    free(abs_path_dir);
}
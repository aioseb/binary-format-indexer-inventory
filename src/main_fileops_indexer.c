#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#define PATH_MAX 256
char resolved_path[PATH_MAX + 1];   // Buffer folosit pentru caile absolute in timpul recursiei (pentru a economisi memorie)
int fd_db = -1;  // File decsriptor-ul global pentru baza de date

struct db_entry{
    const char* absolute_path;
    const char* type;
    off_t size_bytes;
    time_t mtime;
    unsigned long long hash;
    dev_t dev;
    ino_t inode;
};

int validate_directory(const char* path){
    if(path == NULL){
        fprintf(stderr, "Eroare: directorul sursa nu a fost introdus!\n");
        return 2;
    }

    // Verificam daca <dir> este un director, daca exista, si daca poate fi citit
    struct stat dir_st;
    if(stat(path, &dir_st) == 0){
        if(!S_ISDIR(dir_st.st_mode)){
            fprintf(stderr, "Eroare: directorul sursa nu exista sau nu este de tip 'directory'!\n");
            return 3;
        }
        if(access(path, R_OK | X_OK) != 0){
            fprintf(stderr, "Eroare: nu exista drepturi de citire si/sau transversare pentru directorul dat!\n");
            return 1;
        }
    } else{
        perror("Eroare stat");
        return 1;
    }

    return 0;
}

int validate_destination(const char* path){
    if(path == NULL){
        fprintf(stderr, "Eroare: destinatia nu a fost introdusa!\n");
        return 2;
    }

    // Validam destinatia. Daca aceasta nu exista, cream o baza de date noua
    // Ne asiguram ca aceasta poate fi suprascrisa.
    int fd;
    if(-1 == (fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644))){
        perror("Eroare open");
        return 1;
    }
    close(fd);

    return 0;
}

// Implementare FNV-1a hash pentru 64 biti
unsigned long long fnv1a(const char* str){
    unsigned long long hash = 14695981039346656037ULL;
    const unsigned long long prime = 1099511628211ULL;

    for(int i = 0; str[i]; i++){
        hash ^= str[i];
        hash *= prime;
    }

    return hash;
}

// Construieste o intrare pentru fisierul respectiv
int build_entry(const char* path, struct db_entry* entry){
    struct stat st;
    if(0 != lstat(path, &st)){
        perror("Eroare stat entry");
        return 1;
    }

    entry->absolute_path = path;
    entry->size_bytes = st.st_size;
    entry->mtime = st.st_mtime;
    entry->hash = fnv1a(path);
    entry->dev = st.st_dev;
    entry->inode = st.st_ino;

    // Determina tipul fisierului
    if(S_ISREG(st.st_mode)) entry->type = "regular";
    else if(S_ISDIR(st.st_mode)) entry->type = "directory";
    else if(S_ISLNK(st.st_mode)) entry->type = "symlink";
    else if(S_ISCHR(st.st_mode)) entry->type = "characterdev";
    else if(S_ISBLK(st.st_mode)) entry->type = "blockdev";
    else if(S_ISFIFO(st.st_mode)) entry->type = "fifo";
    else if(S_ISSOCK(st.st_mode)) entry->type = "socket";
    else entry->type = "unknown";

    return 0;
}

// Adauga o intrare in baza de date
int append_entry(struct db_entry* entry){
    if(fd_db < 0){
        fprintf(stderr, "Eroare: baza de date nu este deschisa!\n");
        exit(2);
    }

    write(fd_db, entry->absolute_path, sizeof(entry->absolute_path));
    write(fd_db, " ", 1);

    write(fd_db, entry->type, sizeof(entry->type));
    write(fd_db, " ", 1);

    write(fd_db, "\n", 1);
}

// Construieste in maniera recursiva baza de date in <dest>
int build_database(const char* dir, const char* dest){
    DIR* dir_stream;
    struct dirent *ent;
    struct stat st;
    struct db_entry entry;

    char* abs_path_dir = realpath(dir, NULL);
    printf("%s\n", abs_path_dir);

    if(NULL != (dir_stream = opendir(abs_path_dir))){
        while(NULL != (ent = readdir(dir_stream))){
            if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0){ // Ignora . si ..
                continue;
            }

            // Construieste calea absoluta in "resolved_path"
            snprintf(resolved_path, sizeof(resolved_path), "%s/%s", abs_path_dir, ent->d_name);
            build_entry(resolved_path, &entry);
            append_entry(&entry);
            
            if(0 != lstat(resolved_path, &st)){
                perror("Eroare stat build_database");
                exit(1);
            };
            if(S_ISDIR(st.st_mode)){
                build_database(resolved_path, dest);    // Parcurge recursiv directorul curent
            }
        }

        closedir(dir_stream);   
    }
    else{
        perror("Eroare deschidere director");
        return 3;
    }

    free(abs_path_dir);
    return 0;
}

int main(int argc, char** argv){
    if (argc <= 2){
        fprintf(stderr, "Folosire: %s --root <dir> [--db <path>]\n", argv[0]);
        exit(1);
    }

    printf("TODO: main_fileops_indexer.c\n");   
    const char* dir = NULL;
    const char* dest = "./data/index.db";

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
        }
    }

    if(0 != validate_directory(dir)){
        fprintf(stderr, "Eroare: validare director\n");
        exit(1);
    };

    if(0 != validate_destination(dest)){
        fprintf(stderr, "Eroare: validare destinatie\n");
        exit(1);
    };

    if(-1 == (fd_db = open(dest, O_WRONLY | O_TRUNC))){
        perror("Eroare deschidere destinatie");
        exit(1);
    }

    build_database(dir, dest);
    close(fd_db);

    printf("%s %s\n", dir, dest);
}
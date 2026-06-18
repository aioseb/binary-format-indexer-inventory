#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <dirent.h>
#include "../include/db_types.h"
#include "../include/db_utils.h"
#include "../include/db_fileops_utils.h"

// Checksum-ul unui continut folosind fnv1a.
// Returneaza checksum-ul in caz de succes, sau 0 in caz de esec
unsigned long long checksum_content(const char* path, size_t file_size){
    if(file_size == 0){
        return 0;
    }

    int fd = open(path, O_RDONLY);
    if(fd == -1){
        perror("Eroare: deschidere in checksum_content");
        return 0;
    }

    void* addr = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if(addr == MAP_FAILED){
        fprintf(stderr, "Eroare: mmap in checksum_content\n");
        return 0;
    }

    unsigned long long hash = 14695981039346656037ULL;
    const unsigned long long prime = 1099511628211ULL;
    unsigned char* ptr = (unsigned char*)addr;

    for(size_t i = 0; i < file_size; i++){
        hash ^= ptr[i];
        hash *= prime;
    }

    munmap(addr, file_size);
    return hash;
}

// Implementare FNV-1a hash pentru 64 biti
unsigned long long fnv1a(const char* str, int len){
    unsigned long long hash = 14695981039346656037ULL;
    const unsigned long long prime = 1099511628211ULL;

    // Verifica pana la newline
    for(int i = 0; i < len; i++){
        hash ^= str[i];
        hash *= prime;
    }

    return hash;
}

// Parseaza informatiile fisierului/directorului curent in 'entry'
int build_fileops_entry(const char* path, struct db_fileops_entry* entry){
    struct stat st;
    if(0 != lstat(path, &st)){
        perror("Eroare lstat entry");
        return 1;
    }

    uint32_t path_len = strlen(path);
    if(path_len >= PATH_MAX){
        path_len = PATH_MAX - 1;
    }

    entry->size_bytes = (uint64_t)st.st_size;
    entry->mtime = (uint32_t)st.st_mtime;
    entry->hash = fnv1a(path, path_len);
    entry->dev = (uint32_t)st.st_dev;
    entry->inode = (uint32_t)st.st_ino;
    entry->next_entry_bucket = 0;

    entry->absolute_path = malloc(path_len + 1);
    if(!entry->absolute_path){
        return 1;   // Nu s-a putut aloca memorie pentru calea absoluta
    }

    memcpy(entry->absolute_path, path, path_len + 1);
    entry->absolute_path[path_len] = '\0';  // Asiguram null-termination

    entry->path_len = path_len + 1;
    entry->entry_size = entry->path_len + TYPE_LEN_MAX + CHECKSUM_LEN + 6 * sizeof(uint32_t) + 2 * sizeof(uint64_t);  
    
    // Determina tipul fisierului
    if(S_ISREG(st.st_mode)) strncpy(entry->type, "regular", TYPE_LEN_MAX);
    else if(S_ISDIR(st.st_mode)) strncpy(entry->type, "directory", TYPE_LEN_MAX);
    else if(S_ISLNK(st.st_mode)) strncpy(entry->type, "symlink", TYPE_LEN_MAX);
    else if(S_ISCHR(st.st_mode)) strncpy(entry->type, "chardev", TYPE_LEN_MAX);
    else if(S_ISBLK(st.st_mode)) strncpy(entry->type, "blockdev", TYPE_LEN_MAX);
    else if(S_ISFIFO(st.st_mode)) strncpy(entry->type, "fifo", TYPE_LEN_MAX);
    else if(S_ISSOCK(st.st_mode)) strncpy(entry->type, "socket", TYPE_LEN_MAX);
    entry->type[TYPE_LEN_MAX - 1] = '\0';

    // Calculeaza checksum-ul doar pentru fisiere regulate
    if(S_ISREG(st.st_mode)){
        entry->checksum = 1;    // Setam pe 1 pentru a marca fisierul regulat
    }
    else{
        // Pentru celelalte tipuri de fisiere punem doar 0
        entry->checksum = 0;
    }

    // Daca este symlink, formatul caii absolute va fi <abs_path_sym>-><target_path>
    if(S_ISLNK(st.st_mode)){
        char target[PATH_MAX];
        char temp_path[PATH_MAX];

        ssize_t len = readlink(path, target, sizeof(target) - 1);
        target[len] = '\0';

        strncpy(temp_path, path, PATH_MAX);

        size_t new_len = strlen(temp_path) + strlen(target) + 3; // "->" + '\0'
        if(new_len > PATH_MAX){
            new_len = PATH_MAX - 1;
        }

        entry->absolute_path = realloc(entry->absolute_path, new_len);
        if(!entry->absolute_path){
            return 1;
        }

        snprintf(entry->absolute_path, new_len, "%s->%s", temp_path, target);
        entry->path_len = new_len;
        entry->entry_size = entry->path_len + TYPE_LEN_MAX + CHECKSUM_LEN + 6 * sizeof(uint32_t) + 2 * sizeof(uint64_t);
    }

    return 0;
}

// Pune la sfarsitul bazei de date un fileops_entry
// Returneaza 0 in caz de succes; 1 daca exista deja duplicat; -1 in caz de esec
int append_fileops_entry(int fd_db, struct db_fileops_entry* entry){
    if (fd_db < 0) {
        fprintf(stderr, "Eroare: baza de date nu este deschisa!\n");
        return -1;
    }

    void *field_ptrs[FIELD_FILEOPS_COUNT] = {
        &entry->entry_size,
        &entry->path_len,
        entry->absolute_path,
        &entry->type,
        &entry->checksum,
        &entry->size_bytes,
        &entry->mtime,
        &entry->hash,
        &entry->dev,
        &entry->inode,
        &entry->next_entry_bucket
    };

    uint32_t field_sizes[FIELD_FILEOPS_COUNT] = {
        sizeof(entry->entry_size),
        sizeof(entry->path_len),
        entry->path_len,
        sizeof(entry->type),
        sizeof(entry->checksum),
        sizeof(entry->size_bytes),
        sizeof(entry->mtime),
        sizeof(entry->hash),
        sizeof(entry->dev),
        sizeof(entry->inode),
        sizeof(entry->next_entry_bucket)
    };

    if(entry->checksum == 1){
        entry->checksum = checksum_content(entry->absolute_path, entry->size_bytes);
    }

    char buffer[MAX_ENTRY_SIZE];
    int entry_size = 0;
    for(int i = 0; i < FIELD_FILEOPS_COUNT; i++){
        memcpy(buffer + entry_size, field_ptrs[i], field_sizes[i]);
        entry_size += field_sizes[i];
    }

    if(entry_size != entry->entry_size){
        fprintf(stderr, "Eroare: scriere partiala in buffer in append_fileops_entry\n");
        return -1;
    }

    // Punem lacat peste BUCKET-ul in care se afla entry-ul respectiv
    // pentru a evita situatiile in care doua procese nu detecteaza duplicate
    // si pun acelasi entry de 2 ori.
    int entry_bucket = entry->hash % BUCKET_COUNT;

    // Lacat pentru bucket
    struct flock hl;
    hl.l_type = F_WRLCK;
    hl.l_whence = SEEK_SET;
    hl.l_start = BUCKET_OFFSET + entry_bucket;
    hl.l_len = BUCKET_SIZE;

    // Punem lacatul peste BUCKET-ul curent
    if(-1 == fcntl(fd_db, F_SETLKW, &hl)){
        perror("Eroare: fcntl in append_fileops_entry");
        return -1;
    };

    // Verifica daca exista deja entry-ul
    long long entry_offset = get_header_field(fd_db, BUCKET_HEAD + entry_bucket);
    if(entry_offset == -1){
        fprintf(stderr, "Eroare: citire BUCKET_HEAD + %d", entry_bucket);
        return -1;
    }

    int found_duplicate = 0;
    if(entry_offset != EMPTY_BUCKET){
        entry_offset = entry_offset - IDX_ENTRY_SIZE_OFFSET - HEADER_SIZE;
        do{
            int compare_result = compare_fileops_entries(fd_db, entry, entry_offset);

            if(compare_result == -1){
                fprintf(stderr, "Eroare: cautare duplicate in append_fileops_entry\n");
                return -1;
            }
            else if(compare_result == 0){
                found_duplicate = 1;
                break;  // S-a gasit un duplicat
            }

            entry_offset = next_entry(fd_db, entry_offset, NEXT_IS_BUCKET);

            if(entry_offset == 1){
                fprintf(stderr, "Eroare: parcurgere baza de date in append_fileops_entry\n");
                return -1;
            }
        } while(entry_offset != 0);
    }

    if(found_duplicate == 0){
        // Nu s-a gasit niciun duplicat.
        // Se face scrierea efectiva prin APPEND
        // Actualizam NEXT_ENTRY_POS
        // Actualizam BUCKET_HEAD daca acesta nu a fost initializat
        // Actualizam BUCKET_TAIL de fiecare data

        // Introducerea efectiva al entry-ului curent in baza de date
        write_to_db(fd_db, buffer, entry->entry_size, entry_bucket);
    }

    // Luam lacatul de pe BUCKET-ul in care se afla entry-ul
    hl.l_type = F_UNLCK;
    fcntl(fd_db, F_SETLK, &hl);

    if(found_duplicate == 1){
        return 1;
    }
    return 0;
}

// Compara un entry dat ca parametru cu un entry deja existent in baza de date localizat prin "entry_offset"
// Comparatia se face intre cheile principale (caile absolute)
//  1 daca first_entry != second_entry
//  0 daca first_entry == second_entry
// -1 in caz de eroare
// entry_offset-urile incep de la 0 (fizic, incep de la HEADER_SIZE in cadrul formatului binar)
int compare_fileops_entries(int fd_db, struct db_fileops_entry* entry, unsigned int db_entry_offset){
    db_entry_offset += HEADER_SIZE;
    
    // Comparatia dintre lungimi
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
    if(entry->path_len != db_path_len){
        return 1;
    }

    // Comparatia dintre hash-uri
    unsigned long long db_hash;
    unsigned long long db_hash_offset = IDX_ABS_PATH_OFFSET + TYPE_LEN_MAX + db_path_len +
                                        sizeof(entry->checksum) + sizeof(entry->size_bytes) + sizeof(entry->mtime);

    if(sizeof(uint64_t) != pread(fd_db, &db_hash, sizeof(uint64_t), db_entry_offset + db_hash_offset)){
        fprintf(stderr, "Eroare: citire IDX_HASH la 0x%X\n", db_entry_offset);
        return -1;
    }

    if(entry->hash != db_hash){
        return 1;
    }

    // Comparatia efectiva dintre doua cai absolute
    char* db_path = malloc(db_path_len);

    if(db_path_len != pread(fd_db, db_path, db_path_len, db_entry_offset + IDX_ABS_PATH_OFFSET)){
        fprintf(stderr, "Eroare: citire IDX_ABS_PATH la 0x%X\n", db_entry_offset);
        free(db_path);
        return -1;
    }

    if(memcmp(entry->absolute_path, db_path, db_path_len) != 0){
        free(db_path);
        return 1;
    }

    free(db_path);

    return 0;
}

// Parseaza toate informatiile entry-ului aflat la "entry_offset"
// Returneaza 0 daca operatia s-a efectuat cu succes, sau -1 in caz contrar
int get_fileops_entry(int fd_db, unsigned long long entry_offset, struct db_fileops_entry* entry);

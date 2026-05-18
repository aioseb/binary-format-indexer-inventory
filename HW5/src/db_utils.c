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
#include <time.h>
#include "../include/db_utils.h"
#include "../include/db_types.h"

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
        perror("Eroare: stat in validate_directory");
        return 1;
    }

    return 0;
}

// Deschide in maniera concurenta baza de date
int open_database(const char* path, int magic){
    if(path == NULL){
        fprintf(stderr, "Eroare: destinatia nu a fost introdusa!\n");
        return 2;
    }

    struct flock hl;
    hl.l_type = F_WRLCK;
    hl.l_whence = SEEK_SET;
    hl.l_start = 0;
    hl.l_len = HEADER_SIZE;

    // Validam destinatia. Daca aceasta nu exista sau este SEALED cream o baza de date noua
    // Ne asiguram ca aceasta poate fi suprascrisa.
    int fd = open(path, O_RDWR | O_APPEND | O_CREAT | O_EXCL, 0644);
    // Fisierul este creat o singura data de un singur proces (primul care reuseste)
    if(fd == -1){
        if(errno != EEXIST){
            perror("Eroare open in validate_database");
            return -1;
        }

        if(-1 == (fd = open(path, O_RDWR | O_APPEND ))){
            perror("Eroare open in validate_database");
            return -1;
        }

        // Baza de date deja exista si poate si scrisa si citita cu succes
        
        // Punem lacat peste header
        if(fcntl(fd, F_SETLKW, &hl) == -1){
            perror("Eroare: lock in db_util.c");
            return -1;
        }

        // Verificam daca este SEALED sau OPEN
        int state = get_header_field(fd, STATE);

        if(state == STATE_OPEN){
            // Incrementeaza ACTIVE_WRITERS
            int active_writers = get_header_field(fd, ACTIVE_WRITERS);
            if(active_writers != -1){
                set_header_field(fd, ACTIVE_WRITERS, active_writers + 1);
            }
            else{
                fprintf(stderr, "Eroare: actualizare ACTIVE_WRITERS\n");
            }
        }
        else if(state == STATE_SEALED){
            // Reinitializam baza de date
            int magic = get_header_field(fd, MAGIC);
            ftruncate(fd, 0);
            initialize_snapshot(fd, magic);
        }

        // Luam lacatul de pe header
        hl.l_type = F_UNLCK;
        if(fcntl(fd, F_SETLK, &hl) == -1){
            perror("Eroare: unlock in db_util.c");
            return -1;
        }
        hl.l_type = F_WRLCK;
    }
    else{
        // Initializare header de catre primul proces care creeaza baza de date
        if(fcntl(fd, F_SETLKW, &hl) == -1){
            perror("Eroare: lock in db_util.c");
            return -1;
        }

        if(0 != initialize_snapshot(fd, magic)){
            fprintf(stderr, "Eroare: initialize_header in open_database\n");
            return -1;
        }

        hl.l_type = F_UNLCK;
        if(fcntl(fd, F_SETLK, &hl) == -1){
            perror("Eroare: unlock in db_util.c");
            return -1;
        }
        hl.l_type = F_WRLCK;
    }

    return fd;
}

// Inchide baza de date prin file descriptorul acesteia
// Returneaza 0 daca s-a inchis cu succes, sau -1 in caz contrar
int close_database(int fd_db, int use_explicit_path){
    // Verifica formatul fisierului
    int magic = get_header_field(fd_db, MAGIC);

    if(magic == -1){
        fprintf(stderr, "Eroare: format necunoscut\n");
        return -1;
    }

    struct flock hl;

    hl.l_type = F_WRLCK;
    hl.l_whence = SEEK_SET;
    hl.l_start = ACTIVE_WRITERS_OFFSET - 1;
    hl.l_len = 5;   // Cuprinde si STATE-ul fisierului
    // Decrementeaza ACTIVE_WRITERS

    if(fcntl(fd_db, F_SETLKW, &hl) == -1){
        perror("Eroare: lock in db_util.c");
        return -1;
    }

    int active_writers = get_header_field(fd_db, ACTIVE_WRITERS);
    set_header_field(fd_db, ACTIVE_WRITERS, active_writers - 1);

    // Daca nu mai avem ACTIVE_WRITERS, atunci seteaza snapshot-ul ca SEALED
    if(active_writers - 1 == 0){
        set_header_field(fd_db, STATE, STATE_SEALED);
        uint32_t id = get_header_field(fd_db, SNAPSHOT_ID);

        // Redenumeste baza de date in formatul: ["index"|"proc"]_<id>
        char path[PATH_MAX];
        char fd_path[64];

        snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd_db);
        ssize_t len = readlink(fd_path, path, sizeof(path) - 1);
        path[len] = '\0';

        // Actualizeaza numele doar daca acesta este default ("index.db" sau "proc.db")
        char* real_name = strrchr(path, '/');
        real_name++;

        if(use_explicit_path == 0){
            char db_path[PATH_MAX];
            char new_name[PATH_MAX];

            strcpy(db_path, path);
            char* last_slash = strrchr(db_path, '/');
            *last_slash = '\0';
            
            if(magic == FILEOPS_MAGIC_ID){
                snprintf(new_name, sizeof(new_name), "%s/index_%u.db", db_path, id);
            }
            else if(magic == PROC_MAGIC_ID){
                snprintf(new_name, sizeof(new_name), "%s/proc_%u.db", db_path, id);
            }
            
            rename(path, new_name);
        }

        printf("S-a terminat scrierea in baza de date cu ID-ul: %u\n", id);
    }

    hl.l_type = F_UNLCK;
    if(fcntl(fd_db, F_SETLK, &hl) == -1){
        perror("Eroare: unlock in db_util.c");
        return -1;
    }
    hl.l_type = F_WRLCK;

    close(fd_db);
    return 0;
}

// Preia informatii din header
// Returneaza -1 daca a esuat
long long get_header_field(int fd_db, int field){
    if(fd_db == -1){
        fprintf(stderr, "Eroare: baza de date nu este deschisa!\n");
        return -1;
    }
    
    if(field < 0 || field >= FIELD_HEADER_COUNT){
        fprintf(stderr, "Eroare: campul nu exista!\n");
        return -1;
    }
    
    // Iesim temporar din modul APPEND pentru a citi relativ cu inceputul bazei de date
    int flags = fcntl(fd_db, F_GETFL);
    fcntl(fd_db, F_SETFL, flags & ~O_APPEND);
    
    long long ret_value;
    uint8_t val8;
    uint32_t val32;

    switch(field){
    case MAGIC:
        // Valideaza magic bytes
        char magic[5];

        if(MAGIC_SIZE != pread(fd_db, magic, MAGIC_SIZE, MAGIC_OFFSET)){
            fprintf(stderr, "Eroare: citire MAGIC din header\n");
            ret_value = -1;
            break;
        }

        magic[4] = '\0';
        if(strcmp(magic, FILEOPS_MAGIC) == 0){
            ret_value = FILEOPS_MAGIC_ID;   // Baza de date este un fileops_indexer
        }
        else if(strcmp(magic, PROC_MAGIC) == 0){
            ret_value = PROC_MAGIC_ID;   // Baza de date este un proc_snapshot
        }
        else{
            ret_value = -1;  // Format necunoscut
        }
        break;
    
    case VERSION:
        if(VERSION_SIZE != pread(fd_db, &val32, VERSION_SIZE, VERSION_OFFSET)){
            fprintf(stderr, "Eroare: citire VERSION din header\n");
            ret_value = -1;
        }
        else{
            ret_value = val32;
        }
        break;

    case SNAPSHOT_ID:
        if(SNAPSHOT_ID_SIZE != pread(fd_db, &val32, SNAPSHOT_ID_SIZE, SNAPSHOT_ID_OFFSET)){
            fprintf(stderr, "Eroare: citire SNAPSHOT_ID din header\n");
            ret_value = -1;
        }
        else{
            ret_value = val32;
        }
        break;

    case STATE:
        if(STATE_SIZE != pread(fd_db, &val8, STATE_SIZE, STATE_OFFSET)){
            fprintf(stderr, "Eroare: citire STATE din header\n");
            ret_value = -1;
        }
        else{
            ret_value = val8;
        }
        break;

    case ACTIVE_WRITERS:
        if(ACTIVE_WRITERS_SIZE != pread(fd_db, &val32, ACTIVE_WRITERS_SIZE, ACTIVE_WRITERS_OFFSET)){
            fprintf(stderr, "Eroare: citire ACTIVE_WRITERS din header\n");
            ret_value = -1;
        }
        else{
            ret_value = val32;
        }
        break;

    case RECORD_COUNT:
        if(RECORD_COUNT_SIZE != pread(fd_db, &val32, RECORD_COUNT_SIZE, RECORD_COUNT_OFFSET)){
            fprintf(stderr, "Eroare: citire RECORD_COUNT din header\n");
            ret_value = -1;
        }
        else{
            ret_value = val32;
        }
        break;

    case FIRST_ENTRY_POS:
        if(FIRST_ENTRY_POS_SIZE != pread(fd_db, &val32, FIRST_ENTRY_POS_SIZE, FIRST_ENTRY_POS_OFFSET)){
            fprintf(stderr, "Eroare: citire NEXT_ENTRY_POS din header\n");
            ret_value = -1;
        }
        else{
            ret_value = val32;
        }
        break;

    default:
        if(BUCKET <= field && field < BUCKET + BUCKET_COUNT){
            if(BUCKET_SIZE != pread(fd_db, &val8, BUCKET_SIZE, BUCKET_OFFSET + (field - BUCKET))){
                fprintf(stderr, "Eroare: citire BUCKET + %d din header\n", field - BUCKET);
                ret_value = -1;
            }
            else{
                ret_value = val8;
            }
        }
        else if(BUCKET_HEAD <= field && field < FIELD_HEADER_COUNT){
            if(BUCKET_HEAD_SIZE != pread(fd_db, &val32, BUCKET_HEAD_SIZE, BUCKET_HEAD_OFFSET + BUCKET_HEAD_SIZE * (field - BUCKET_HEAD))){
                fprintf(stderr, "Eroare: citire BUCKET HEAD/TAIL + %d din header\n", field - BUCKET_HEAD);
                ret_value = -1;
            }
            else{
                ret_value = val32;
            }
        }
        else{
            ret_value = -1;  // Camp necunoscut
        }
    }

    // Reintroducem modul APPEND
    fcntl(fd_db, F_SETFL, flags);
    return ret_value;
}

// Seteaza campurile header-ului
// Returneaza 0 daca campul a fost modificat cu succes, -1 in caz contrar
int set_header_field(int fd_db, int field, unsigned long long value){
    if(fd_db == -1){
        fprintf(stderr, "Eroare: baza de date nu este deschisa!\n");
        return -1;
    }
    
    if(field < 0 || field >= FIELD_HEADER_COUNT){
        fprintf(stderr, "Eroare: campul nu exista!\n");
        return -1;
    }

    // Iesim temporar din modul APPEND pentru a scrie relativ cu inceputul bazei de date
    int flags = fcntl(fd_db, F_GETFL);
    fcntl(fd_db, F_SETFL, flags & ~O_APPEND);

    int ret_value = 0;
    uint8_t val8 = (uint8_t)value;
    uint32_t val32 = (uint32_t)value;

    switch(field){
    case MAGIC:
        // Valideaza magic bytes
        const char* magic;
        if(value == FILEOPS_MAGIC_ID){
            magic = "IDX1";
        }
        else if(value == PROC_MAGIC_ID){
            magic = "PRC1";
        }
        else{
            fprintf(stderr, "Eroare: file format necunoscut\n");
            ret_value = -1;
        }

        if(MAGIC_SIZE != pwrite(fd_db, magic, MAGIC_SIZE, MAGIC_OFFSET)){
            fprintf(stderr, "Eroare: scriere MAGIC in header\n");
            ret_value = -1;
        }
        break;
    
    case VERSION:
        if(VERSION_SIZE != pwrite(fd_db, &val32, VERSION_SIZE, VERSION_OFFSET)){
            fprintf(stderr, "Eroare: scriere VERSION in header\n");
            ret_value = -1;
        }
        break;

    case SNAPSHOT_ID:
        if(SNAPSHOT_ID_SIZE != pwrite(fd_db, &val32, SNAPSHOT_ID_SIZE, SNAPSHOT_ID_OFFSET)){
            fprintf(stderr, "Eroare: scriere SNAPSHOT_ID in header\n");
            ret_value = -1;
        }
        break;

    case STATE:
        if(val8 != STATE_OPEN && val8 != STATE_SEALED){
            fprintf(stderr, "Eroare: scriere STATE necunoscut in header\n");
            ret_value = -1;
            break;
        }

        if(STATE_SIZE != pwrite(fd_db, &val8, STATE_SIZE, STATE_OFFSET)){
            fprintf(stderr, "Eroare: scriere STATE in header\n");
            ret_value = -1;
        }
        break;

    case ACTIVE_WRITERS:
        if(ACTIVE_WRITERS_SIZE != pwrite(fd_db, &val32, ACTIVE_WRITERS_SIZE, ACTIVE_WRITERS_OFFSET)){
            fprintf(stderr, "Eroare: scriere ACTIVE_WRITERS in header\n");
            ret_value = -1;
        }
        break;

    case RECORD_COUNT:
        if(RECORD_COUNT_SIZE != pwrite(fd_db, &val32, RECORD_COUNT_SIZE, RECORD_COUNT_OFFSET)){
            fprintf(stderr, "Eroare: scriere RECORD_COUNT in header\n");
            ret_value = -1;
        }
        break;

    case FIRST_ENTRY_POS:
        if(FIRST_ENTRY_POS_SIZE != pwrite(fd_db, &val32, FIRST_ENTRY_POS_SIZE, FIRST_ENTRY_POS_OFFSET)){
            fprintf(stderr, "Eroare: scriere NEXT_ENTRY_POS in header\n");
            ret_value = -1;
        }
        break;

    default:
        if(BUCKET <= field && field < BUCKET_HEAD){
            if(BUCKET_SIZE != pwrite(fd_db, &val8, BUCKET_SIZE, BUCKET_OFFSET + (field - BUCKET))){
                fprintf(stderr, "Eroare: scriere BUCKET + %d in header\n", field - BUCKET);
                ret_value = -1;
            }
        }
        else if(BUCKET_HEAD <= field && field < FIELD_HEADER_COUNT){
            if(BUCKET_HEAD_SIZE != pwrite(fd_db, &val32, BUCKET_HEAD_SIZE, BUCKET_HEAD_OFFSET + BUCKET_HEAD_SIZE * (field - BUCKET_HEAD))){
                fprintf(stderr, "Eroare: scriere BUCKET HEAD/TAIL + %d din header\n", field - BUCKET_HEAD);
                ret_value = -1;
            }
        }
        else{
            ret_value = -1;  // Camp necunoscut
        }
    }

    // Reintroducem modul APPEND
    fcntl(fd_db, F_SETFL, flags);
    return ret_value;
}

// Initializeaza header-ul bazei de date
// Returneaza '0' daca se efectueaza cu succes, sau -1 pentur esec
int initialize_snapshot(int fd_db, int magic){
    if(0 != set_header_field(fd_db, MAGIC, magic)){
        fprintf(stderr, "Eroare: initializare header\n");
        return -1;
    } 

    int ret1 = set_header_field(fd_db, VERSION, VERSION_VALUE);
    int ret2 = set_header_field(fd_db, SNAPSHOT_ID, time(0));
    int ret3 = set_header_field(fd_db, STATE, STATE_OPEN);
    int ret4 = set_header_field(fd_db, ACTIVE_WRITERS, 1);
    int ret5 = set_header_field(fd_db, RECORD_COUNT, 0);
    int ret6 = set_header_field(fd_db, FIRST_ENTRY_POS, HEADER_SIZE);

    if(ret1 || ret2 || ret3 || ret4 || ret5 || ret6){
        fprintf(stderr, "Eroare: initializare header\n");
        return -1;
    }

    // Alocam memorie pentru bucket-uri (regiunea care va fi locked;
    // scrierea inregistrarilor este atomica, acest lucru fiind asigurat
    // de modul O_APPEND)
    for(int bucket = BUCKET; bucket < BUCKET_HEAD; bucket++){
        if(0 != set_header_field(fd_db, bucket, 'B')){
            fprintf(stderr, "Eroare: initializare header pentru BUCKET\n");
            return -1;
        }
    }

    // Bucket-uri HEAD
    for(int bucket = BUCKET_HEAD; bucket < BUCKET_TAIL; bucket++){
        if(0 != set_header_field(fd_db, bucket, EMPTY_BUCKET)){
            fprintf(stderr, "Eroare: initializare header pentru BUCKET_HEAD\n");
            return -1;
        }
    }

    // Bucket-uri TAIL
    for(int bucket = BUCKET_TAIL; bucket < FIELD_HEADER_COUNT; bucket++){
        if(0 != set_header_field(fd_db, bucket, EMPTY_BUCKET)){
            fprintf(stderr, "Eroare: initializare header pentru BUCKET_TAIL\n");
            return -1;
        }
    }

    return 0;
}

// Cauta pozitia urmatorului entry
// Daca next_is_regular = NEXT_IS_REGULAR, se face parcurgerea normala
// Daca next_is_regular = NEXT_IS_BUCKET, se face parcurgerea in cadrul BUCKET-ului in care se afla inregistrarea din "entry_offset"
// Returneaza OFFSET-Ul urmatorului entry, 0 daca s-a ajuns la EOF, sau 1 in caz de eroare
unsigned int next_entry(int fd_db, unsigned int entry_offset, int next_is_regular){
    unsigned int offset = HEADER_SIZE + entry_offset;
    unsigned int entry_size = 0;

    int bytes_read = pread(fd_db, &entry_size, sizeof(uint32_t), offset);

    // O solutie pentru a preveni eventualele citiri gresite in baza de date
    if(entry_size > MAX_ENTRY_SIZE){
        fprintf(stderr, "Eroare: entry_size-ul citit este mai mare decat MAX_ENTRY_SIZE\n");
        return 1;
    }

    if(bytes_read == 0){
        // S-a ajuns cu succes la EOF
        return 0;
    }
    else if(bytes_read != sizeof(uint32_t)){
        fprintf(stderr, "Eroare: citire entry_size la offset: %llu + HEADER_SIZE\n", entry_offset);
        return 1;
    }

    if(next_is_regular == NEXT_IS_REGULAR){
        return entry_size + entry_offset;   // Returneaza pozitia urmatorului entry
    }
    else if(next_is_regular == NEXT_IS_BUCKET){
        uint32_t next_entry_bucket_pos; 
        // IDX_NEXT_ENTRY_BUCKET este ultimul camp de dimensiune 4
        bytes_read = pread(fd_db, &next_entry_bucket_pos, sizeof(uint32_t), offset + entry_size - sizeof(uint32_t));
        // printf("%X\n", next_entry_bucket_pos);
        
        if(next_entry_bucket_pos == 0){
            // S-a ajuns cu succes la finalul BUCKET-ului
            return 0;
        }
        else if(bytes_read != sizeof(uint32_t)){
            fprintf(stderr, "Eroare: citire next_entry_bucket_pos la offset: %llu + HEADER_SIZE\n", entry_offset);
            return 1;
        }

        return next_entry_bucket_pos - HEADER_SIZE;
    }
    else{
        fprintf(stderr, "Eroare: Modalitate de parcurgere necunoscuta in next_fileops_entry\n");
        return 1;
    }
}

// Scrie in baza de date un entry care se afla in BUCKET-ul "entry_bucket"
// Entry-ul este dat intr-un buffer
// Returneaza 0 in caz de succees, sau -1 in caz de esec
// IMPORTANT: Se pune lacat peste BUCKET + entry_bucket dinainte!
int write_to_db(int fd_db, char buffer[], unsigned int entry_size, int entry_bucket){
    // Introducerea efectiva al entry-ului curent in baza de date
    ssize_t write_bytes = write(fd_db, buffer, entry_size);
    if(write_bytes != (ssize_t)entry_size){
        perror("Eroare: scriere partiala al unui entry in append_fileops_entry");
        return -1;
    }
    long long next_entry_pos = lseek(fd_db, 0, SEEK_CUR) - entry_size;

    // Deoarece "entry_bucket" are deja lacat nu este nevoie sa mai punem unul
    // pentru BUCKET_HEAD si BUCKET_TAIL; ele vor fi modificate si citite doar
    // de un singur proces cu entry-ul in BUCKET-ul "entry_bucket"
    long long bucket_head = get_header_field(fd_db, BUCKET_HEAD + entry_bucket);
    if(bucket_head == -1){
        fprintf(stderr, "Eroare: preluare BUCKET_HEAD + %d in append_fileops_entry\n", entry_bucket);
        return -1;
    }
    else if(bucket_head == EMPTY_BUCKET){
        // Initializare BUCKET_HEAD + entry_bucket
        if(-1 == set_header_field(fd_db, BUCKET_HEAD + entry_bucket, next_entry_pos)){
            fprintf(stderr, "Eroare: actualizare BUCKET_HEAD + %d in append_fileops_entry\n", entry_bucket);
            return -1;
        }
    }

    // Actualizare BUCKET_TAIL + entry_bucket
    long long bucket_tail = get_header_field(fd_db, BUCKET_TAIL + entry_bucket);

    if(bucket_tail == -1){
        fprintf(stderr, "Eroare: preluare BUCKET_TAIL + %d\n", entry_bucket);
        return -1;
    }

    if(-1 == set_header_field(fd_db, BUCKET_TAIL + entry_bucket, next_entry_pos)){
        fprintf(stderr, "Eroare: actualizare BUCKET_TAIL + %d\n", entry_bucket);
        return -1;
    }
    
    if(bucket_tail != EMPTY_BUCKET){
        // Actualizare next_entry_bucket al ultimului entry din BUCKET-ul entry_bucket
        // Dezactivare temporara al modului APPEND
        // Pentru ca se modifica doar un entry din acelasi BUCKET ca entry-ul ce urmeaza a fi inserat,
        // este destul doar un lacat peste BUCKET + entry_size
        int flags = fcntl(fd_db, F_GETFL);
        fcntl(fd_db, F_SETFL, flags & ~O_APPEND);
        unsigned int bucket_entry_tail_size;

        if(sizeof(uint32_t) != pread(fd_db, &bucket_entry_tail_size, sizeof(uint32_t), bucket_tail)){
            fprintf(stderr, "Eroare: citire entry_size-ul de la offset-ul %d\n", bucket_tail);
            return -1;
        }

        unsigned int bucket_entry_next_pos = bucket_tail + bucket_entry_tail_size - sizeof(uint32_t);
        if(sizeof(uint32_t) != pwrite(fd_db, &next_entry_pos, sizeof(uint32_t), bucket_entry_next_pos)){
            fprintf(stderr, "Eroare: actualizare next_entry_bucket la offset-ul %d\n", bucket_entry_next_pos);
            return -1;
        }

        fcntl(fd_db, F_SETFL, flags);   // Reintroducem APPEND
    }
}
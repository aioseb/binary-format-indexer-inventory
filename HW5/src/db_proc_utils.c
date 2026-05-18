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

// Parseaza informatiei procesului in "entry"
int build_proc_entry(int pid, struct db_proc_entry* entry){
    char path[LEN_NAME_MAX];
    char comm[LEN_NAME_MAX];
    char cmdline[LEN_CMDLINE_MAX];
    char stat_buf[LEN_STAT_MAX];
    int read_bytes;
    char chr;
    int fd;

    uint32_t comm_size = 0;
    uint32_t cmdline_size = 0;

    // comm
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    if(-1 == (fd = open(path, O_RDONLY))){
        if(errno == ENOENT || errno == ESRCH){
            return 1;   // Procesul a disparut
        }
        perror("Eroare: open in build_proc_entry (comm)");
        return -1;
    }

    while(0 < (read_bytes = read(fd, &chr, sizeof(char))) && comm_size < LEN_NAME_MAX){
        comm[comm_size] = chr;
        comm_size++;
    }
    if(read_bytes == -1){
        close(fd);
        return 1;
    }

    if(comm_size < LEN_NAME_MAX){ // Ultimul caracter al unui comm este un newline; il inlocuim
        comm[comm_size - 1] = '\0';
    }
    else{
        comm[LEN_NAME_MAX - 1] = '\0';
        comm_size = LEN_NAME_MAX;
    }
    close(fd);

    // cmdline
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    if(-1 == (fd = open(path, O_RDONLY))){
        if(errno == ENOENT || errno == ESRCH){
            return 1;   // Procesul a disparut
        }
        perror("Eroare: open in build_proc_entry (cmdline)");
        return -1;
    }

    while(0 < (read_bytes = read(fd, &chr, sizeof(char))) && cmdline_size < LEN_CMDLINE_MAX){
        if(chr != '\0'){
            cmdline[cmdline_size] = chr;
        }
        else{
            cmdline[cmdline_size] = ' ';
        }
        cmdline_size++; // Argumentele cmdline-ului sunt separate de '\0'; le inlocuim cu whitespace
    }
    if(read_bytes == -1){
        close(fd);
        return 1;
    }

    if(cmdline_size < LEN_CMDLINE_MAX){
        cmdline[cmdline_size] = '\0';
        cmdline_size++;
    }
    else{
        cmdline[LEN_CMDLINE_MAX - 1] = '\0';
        cmdline_size = LEN_CMDLINE_MAX;
    }
    close(fd);

    // stat (pid, ppid, state, rss, cpu time(user + kernel))
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if(-1 == (fd = open(path, O_RDONLY))){
        if(errno == ENOENT || errno == ESRCH){
            return 1;   // Procesul a disparut
        }
        perror("Eroare: open in build_proc_entry (stat)");
        return -1;
    }

    if(-1 == (read_bytes = read(fd, stat_buf, LEN_STAT_MAX))){
        close(fd);
        return 1;
    }
    stat_buf[read_bytes] = '\0';
    close(fd);

    // Pune informatiile din stat in proc_entry
    char* other = strrchr(stat_buf, ')');   // comm-ul din stat este intre paranteze si
                                            // poate avea spatii libere, ce va duce la parsare eronata

    uint64_t user_time;
    uint64_t kernel_time;
    sscanf(other + 2,
                   "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*u %*u %*u %*u %*d %*d %*d %*d %llu",
                   &entry->state,
                   &entry->ppid,
                   &user_time,
                   &kernel_time,
                   &entry->rss);
    entry->cpu_time = user_time + kernel_time;  // Timp total
    entry->pid = pid;

    entry->comm_size = comm_size;
    entry->cmdline_size = cmdline_size;

    entry->comm = malloc(comm_size);
    if(entry->comm == NULL){
        perror("Eroare: alocare memorie comm");
        return 1;
    }
    memcpy(entry->comm, comm, comm_size);
    
    entry->cmdline = malloc(cmdline_size);
    if(entry->cmdline == NULL){
        free(entry->comm);
        perror("Eroare: alocare memorie cmdline");
        return -1;
    }
    memcpy(entry->cmdline, cmdline, cmdline_size);

    strcat(path, "(field 24)");
    entry->rss_src_size = strlen(path) + 1;
    entry->rss_src = malloc(entry->rss_src_size);
    if(entry->rss_src == NULL){
        free(entry->cmdline);
        free(entry->comm);
        perror("Eroare: alocare memorie rss_src");
        return -1;
    }
    memcpy(entry->rss_src, path, entry->rss_src_size);

    entry->entry_size = sizeof(uint8_t) + 7 * sizeof(uint32_t) + 2 * sizeof(uint64_t) + 
                        comm_size + cmdline_size + entry->rss_src_size;

    entry->next_entry_bucket = 0;
    
    return 0;
}

// Pune la sfarsitul bazei de date un proc_entry
// Returneaza 0 in caz de succes; 1 daca exista deja duplicat; -1 in caz de esec
int append_proc_entry(int fd_db, struct db_proc_entry* entry){
    if(fd_db < 0){
        fprintf(stderr, "Eroare: baza de date nu este deschisa!\n");
        return 2;
    }

    void* field_ptrs[FIELD_PROC_COUNT] = {
        &entry->entry_size,
        &entry->pid,
        &entry->ppid,
        &entry->state,
        &entry->comm_size,
        entry->comm,
        &entry->cmdline_size,
        entry->cmdline,
        &entry->rss_src_size,
        entry->rss_src,
        &entry->rss,
        &entry->cpu_time,
        &entry->next_entry_bucket
    };

    uint32_t field_sizes[FIELD_PROC_COUNT] = {
        sizeof(entry->entry_size),
        sizeof(entry->pid),
        sizeof(entry->ppid),
        sizeof(entry->state),
        sizeof(entry->comm_size),
        entry->comm_size,
        sizeof(entry->cmdline_size),
        entry->cmdline_size,
        sizeof(entry->rss_src_size),
        entry->rss_src_size,
        sizeof(entry->rss),
        sizeof(entry->cpu_time),
        sizeof(entry->next_entry_bucket)
    };

    // Creeaza un buffer cu toate informatiile serializate
    char buffer[MAX_ENTRY_SIZE];
    int entry_size = 0;
    for(int i = 0; i < FIELD_PROC_COUNT; i++){
        memcpy(buffer + entry_size, field_ptrs[i], field_sizes[i]);
        entry_size += field_sizes[i];
    }

    if(entry_size != entry->entry_size){
        fprintf(stderr, "Eroare: scriere partiala in buffer in append_proc_entry\n");
        return 1;
    }

    // Punem lacat peste BUCKET-ul in care se afla entry-ul respectiv
    // pentru a evita situatiile in care doua procese nu detecteaza duplicate
    // si pun acelasi entry de 2 ori.
    int entry_bucket = entry->pid % BUCKET_COUNT;

    // Lacat pentru bucket
    struct flock hl;
    hl.l_type = F_WRLCK;
    hl.l_whence = SEEK_SET;
    hl.l_start = BUCKET_OFFSET + entry_bucket;
    hl.l_len = BUCKET_SIZE;

    // Punem lacatul peste BUCKET-ul curent
    if(-1 == fcntl(fd_db, F_SETLKW, &hl)){
        perror("Eroare: fcntl in append_proc_entry");
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
        entry_offset = entry_offset - HEADER_SIZE;
        do{
            int compare_result = compare_proc_entries(fd_db, entry, entry_offset);
            
            if(compare_result == -1){
                fprintf(stderr, "Eroare: cautare duplicate in append_proc_entry\n");
                return -1;
            }
            else if(compare_result == 0){
                found_duplicate = 1;
                break;  // S-a gasit un duplicat
            }

            entry_offset = next_entry(fd_db, entry_offset, NEXT_IS_BUCKET);

            if(entry_offset == 1){
                fprintf(stderr, "Eroare: parcurgere baza de date in append_proc_entry\n");
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
// Comparatia se face intre cheile principale (PID-uri)
//  1 daca first_entry != second_entry
//  0 daca first_entry == second_entry
// -1 in caz de eroare
// entry_offset-urile incep de la 0 (fizic, incep de la HEADER_SIZE in cadrul formatului binar)
int compare_proc_entries(int fd_db, struct db_proc_entry* entry, unsigned int db_entry_offset){
    db_entry_offset += HEADER_SIZE;
    
    // Comparatia dintre lungimi
    int db_pid;
    int read_bytes = pread(fd_db, &db_pid, sizeof(uint32_t), db_entry_offset + PROC_PID_OFFSET);

    if(read_bytes != sizeof(uint32_t)){
        if(read_bytes == 0){
            // s-a citit de la EOF
            return 0;
        }
        // printf("%d\n", read_bytes);
        fprintf(stderr, "Eroare: citire PROC_PID la 0x%X\n", db_entry_offset);
        return -1;
    }
    if(entry->pid != db_pid){
        return 1;
    }

    return 0;   // S-au gasit doua PID-uri identice
}

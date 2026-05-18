// Functiile specifice fisierului IPC
//
// Toate aceste functii returneaza:
// 0 in caz de succes
// -1 in caz de esec
//
// In cazul in care o functie returneaza o valoare, aceasta se face printr-un parametru.

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "../include/inv_ipc.h"

// Initializeaza IPC-ul si il mapeaza
int start_ipc(const char* path, int workers, int max_depth, int capacity, int simulate_work_ms, struct shared_data** sd){
    *sd = NULL;

    int fd = open(path, O_CREAT | O_RDWR, 0666);
    if(fd == -1){
        perror("open in start_ipc");
        return -1;
    } 
    
    if(ftruncate(fd, sizeof(struct shared_data)) == -1){
        perror("ftruncate in start_ipc");
        return -1;
    }

    // Initializeaza campurile
    struct shared_data* map_data = mmap(
        NULL,
        sizeof(struct shared_data),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if(map_data == MAP_FAILED){
        perror("mmap in start_ipc");
        return -1;
    }

    memset(map_data, 0, sizeof(*map_data));

    if (init_shared_data(map_data, workers, max_depth, capacity, simulate_work_ms) == -1) {
        fprintf(stderr, "init_shared_data in start_ipc\n");
        munmap(map_data, sizeof(struct shared_data));
        close(fd);
        return -1;
    }

    *sd = map_data;
    close(fd);
    return 0;
}

// Functii de manipulare a IPC-ului
int enqueue_job(const struct job_t* job, struct shared_data** sd){
    // Transforma jobul in cale absoluta
    char abs_path[PATH_MAX];
    if(realpath(job->job_name, abs_path) == NULL){
        perror("realpath in enqueue_job");
        return -1;
    }

    struct job_t temp_job;
    temp_job.depth = job->depth;
    strcpy(temp_job.job_name, abs_path);

    sem_wait(&(*sd)->jq.job_empty);
    sem_wait(&(*sd)->jq.job_mutex);

    (*sd)->jq.jobs[(*sd)->jq.tail_pos++] = temp_job; 
    (*sd)->jq.tail_pos %= CAPACITY;
    (*sd)->hdr.queued_jobs++;

    msync(*sd, sizeof(struct shared_data), MS_SYNC);
        
    sem_post(&(*sd)->jq.job_mutex);
    sem_post(&(*sd)->jq.job_full);

    return 0;
}       

// Citim un job din coada.
// Returneaza 1 daca nu mai este nimic de citit din coada
int dequeue_job(struct job_t* job, struct shared_data** sd){
    // Scoatem din coada un job si il returnam prin intermediul lui job[]
    sem_wait(&(*sd)->jq.job_full);
    
    if ((*sd)->hdr.active == 'E') {
        sem_post(&(*sd)->jq.job_full); // ultimul worker nu mai are ce sa faca; anunta-i pe ceilalti workeri si managerul
        return 1;
    }

    sem_wait(&(*sd)->jq.job_mutex);

    *job = (*sd)->jq.jobs[(*sd)->jq.head_pos++];
    (*sd)->jq.head_pos %= CAPACITY;
    (*sd)->hdr.queued_jobs--;

    msync(*sd, sizeof(struct shared_data), MS_SYNC);

    sem_post(&(*sd)->jq.job_mutex);
    sem_post(&(*sd)->jq.job_empty);

    return 0;
}

// Functii de manipulare a cozii de rezultate

// Pune in coada rezultatelor un rezultat
int enqueue_result(const struct result* res, struct shared_data** sd){
    sem_wait(&(*sd)->rc.result_empty);
    sem_wait(&(*sd)->rc.result_mutex);

    (*sd)->rc.results[(*sd)->rc.tail_pos++] = *res; 
    (*sd)->rc.tail_pos %= CAPACITY;
    (*sd)->hdr.queued_results++;

    sem_post(&(*sd)->rc.result_mutex);
    sem_post(&(*sd)->rc.result_full);

    return 0;
}

// Citeste si scoate din coada rezultatelor un rezultat
// Returneaza 1 daca workerii au terminat activitatea
int dequeue_result(struct result* res, struct shared_data** sd){
    sem_wait(&(*sd)->rc.result_full);

    if((*sd)->hdr.active == 'E'){
        sem_post(&(*sd)->rc.result_full);
        return 1;
    }

    sem_wait(&(*sd)->rc.result_mutex);

    *res = (*sd)->rc.results[(*sd)->rc.head_pos++];
    (*sd)->rc.head_pos %= CAPACITY;
    (*sd)->hdr.queued_results--;

    sem_post(&(*sd)->rc.result_mutex);
    sem_post(&(*sd)->rc.result_empty);

    return 0;
}

// Functii de inserare si citire a statisticilor date de catre workeri
int push_worker_stats(const struct worker_stats* wstats, struct shared_data** sd){
    sem_wait(&(*sd)->ws.worker_stat_mutex);

    if((*sd)->ws.count >= CAPACITY){
        fprintf(stderr, "stiva este full\n");
        return -1;
    }

    (*sd)->ws.wstats[(*sd)->ws.count++] = *wstats;

    sem_post(&(*sd)->ws.worker_stat_mutex);
    return 0;
}

int pop_worker_stats(struct worker_stats* wstats, struct shared_data** sd){
    sem_wait(&(*sd)->ws.worker_stat_mutex);

    if((*sd)->ws.count <= 0){
        fprintf(stderr, "stiva este goala\n");
        return -1;
    }

    *wstats = (*sd)->ws.wstats[--(*sd)->ws.count];

    sem_post(&(*sd)->ws.worker_stat_mutex);
    return 0;
}

// Functie pentru preluarea adresei a informatiei partajate
int get_mapped_data(const char* ipc_path, struct shared_data** sd){
    *sd = NULL;

    int fd = open(ipc_path, O_RDWR, 0666);
    if(fd == -1){
        perror("open in start_ipc");
        return -1;
    } 

    struct shared_data* map_data = mmap(
        NULL,
        sizeof(struct shared_data),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if(map_data == MAP_FAILED){
        perror("mmap in get_mapped_data");
        return -1;
    }

    *sd = map_data;

    if(strcmp(map_data->hdr.magic, "IPC4") != 0){
        fprintf(stderr, "fisier incompatibil! \n");
        munmap(map_data, sizeof(struct shared_data));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

// Functii de initializare

// Incepe executia a unui anumit numar de workeri cu programul fileops_worker
int init_workers(int workers, const char* ipc){
    char worker_id[16];

    for(int worker = 0; worker < workers; worker++){
        pid_t cpid = fork();

        snprintf(worker_id, 10, "%d", worker);

        char* args[] = {
            "../bin/fileops_worker",
            "--worker-id",
            worker_id,
            "--ipc",
            ipc,
            NULL
        };

        if(cpid == 0){
            if(execvp("./bin/fileops_worker", args) == -1){
                perror("execvp in init_workers");
                return -1;
            }
        }
    }

    return 0;
}

// Se initializeaza memoria partajata
int init_shared_data(struct shared_data* sd, int workers, int max_depth, int capacity, int simulate_work_ms){
    if(init_header(&sd->hdr, workers, max_depth, capacity, simulate_work_ms) == -1){
        fprintf(stderr, "init_header in init_shared_data\n");
        return -1;
    }

    if(init_job_queue(&sd->jq) == -1){
        fprintf(stderr, "init_job_queue in init_shared_data\n");
        return -1;
    }

    if(init_result_channels(&sd->rc) == -1){
        fprintf(stderr, "init_result_channels in init_shared_data\n");
        return -1;
    }

    if(init_worker_stack(&sd->ws) == -1){
        fprintf(stderr, "init_worker_stack in init_shared_data\n");
        return -1;
    }

    return 0;
}

// Initializarea headerului
int init_header(struct header* hdr, int workers, int max_depth, int capacity, int simulate_work_ms){
    hdr->magic[0] = 'I';
    hdr->magic[1] = 'P';
    hdr->magic[2] = 'C';
    hdr->magic[3] = '4';
    hdr->magic[4] = '\0';

    hdr->version = VERSION;
    hdr->max_depth = max_depth;
    hdr->workers = workers;
    hdr->capacity = capacity;
    hdr->simulate_work_ms = simulate_work_ms;
    hdr->active = 'A';  // 'A' - activ | 'E' - finalizat
    hdr->queued_jobs = 0;
    hdr->active_jobs = 0;
    hdr->queued_results = 0;

    return 0;
}

// Initializarea cozii pentru joburi
int init_job_queue(struct job_queue* jq){
    jq->capacity = CAPACITY;
    jq->head_pos = 0;
    jq->tail_pos = 0;

    if(sem_init(&jq->job_empty, 1, CAPACITY) == -1){
        perror("sem_init job_empty in init_job_queue");
        return -1;
    }

    if(sem_init(&jq->job_full, 1, 0) == -1){
        perror("sem_init job_full in init_job_queue");
        return -1;
    }

    if(sem_init(&jq->job_mutex, 1, 1) == -1){
        perror("sem_init job_mutex in init_job_queue");
        return -1;
    }

    return 0;
}

// Initializarea canalelor de rezultate
int init_result_channels(struct result_channels* rc){
    rc->capacity = CAPACITY;
    rc->head_pos = 0;
    rc->tail_pos = 0;

    if(sem_init(&rc->result_empty, 1, CAPACITY) == -1){
        perror("sem_init result_empty in init_result_channels");
        return -1;
    }

    if(sem_init(&rc->result_full, 1, 0) == -1){
        perror("sem_init result_full in init_result_channels");
        return -1;
    }

    if(sem_init(&rc->result_mutex, 1, 1) == -1){
        perror("sem_init result_mutex in init_result_channels");
        return -1;
    }

    return 0;
}

int init_worker_stack(struct worker_stack* ws){
    ws->count = 0;
    
    if(sem_init(&ws->worker_stat_mutex, 1, 1) == -1){
        perror("sem_init worker_stat_mutex in init_worker_stack");
        return -1;
    }
    return 0;
}

// Functii specifice bazei de date

// Initializeaza baza de date in care se vor scrie rezultatele consumate de manager
int start_db(const char* db_path, int workers, struct header_db** hd){
    *hd = NULL;

    int fd = open(db_path, O_CREAT | O_RDWR, 0666);
    if(fd == -1){
        perror("open in start_ipc");
        return -1;
    } 
    
    if(ftruncate(fd, sizeof(struct header_db)) == -1){
        perror("ftruncate in start_ipc");
        return -1;
    }

    // Initializeaza campurile
    struct header_db* map_data = mmap(
        NULL,
        sizeof(struct header_db),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if(map_data == MAP_FAILED){
        perror("mmap in start_db");
        return -1;
    }

    memset(map_data, 0, sizeof(*map_data));

    // Initializare cu valori
    *hd = map_data;
    
    (*hd)->magic[0] = 'I';
    (*hd)->magic[1] = 'N';
    (*hd)->magic[2] = 'V';
    (*hd)->magic[3] = '4';
    (*hd)->magic[4] = '\0';

    (*hd)->version = VERSION;
    (*hd)->complete = 0;
    (*hd)->file_record_count = 0;
    (*hd)->worker_count = workers;
    (*hd)->first_record_offset = sizeof(struct header_db);
    (*hd)->first_stat_offset = sizeof(struct header_db);
 
    close(fd);
    return 0;
}

// Pune la final un rezultat
// IMPORTANT: Fisierul trebuie sa fie in modul O_APPEND + intrarile o sa aiba dimensiune dinamica!
int append_result_db(int fd_db, struct result* res, struct header_db** hd){
    int path_len = strlen(res->abs_path) + 1;
    int entry_size = path_len + 2 * sizeof(int) + sizeof(*res) - sizeof(res->abs_path);
    
    void *field_ptrs[9] = {
        &entry_size,
        &path_len,
        &res->abs_path,
        &res->size,
        &res->mtime,
        &res->mode,
        &res->uid,
        &res->gid,
        &res->sha
    };

    int field_sizes[9] = {
        sizeof(entry_size),
        sizeof(path_len),
        path_len,
        sizeof(res->size),
        sizeof(res->mtime),
        sizeof(res->mode),
        sizeof(res->uid),
        sizeof(res->gid),
        sizeof(res->sha)
    };

    unsigned char buffer[2 * PATH_MAX];
    int temp_entry_size = 0;
    for(int i = 0; i < 9; i++){
        memcpy(buffer + temp_entry_size, field_ptrs[i], field_sizes[i]);
        temp_entry_size += field_sizes[i];
    }

    if(temp_entry_size != entry_size){
        fprintf(stderr, "alocare partiala a rezultatului in append_result_db\n");
        return -1;
    }

    temp_entry_size = write(fd_db, buffer, entry_size);
    if(temp_entry_size != entry_size){
        fprintf(stderr, "scriere partiala in append_result_db\n");
        return -1;
    }
    
    (*hd)->first_stat_offset += entry_size;
    return 0;
}

// Pune la final o statistica de la un worker
int append_stats_db(int fd_db, struct worker_stats* ws, struct header_db** hd){
    if(write(fd_db, ws, sizeof(struct worker_stats)) != sizeof(struct worker_stats)){
        fprintf(stderr, "scriere partiala in append_stats_db\n");
        return -1;
    }

    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../include/inv_ipc.h"

struct manager_args{
    char* root;
    int workers;
    char* ipc_path;
    char* db_path;
    int max_depth;
    int simulate_work_ms;
    int db_mode;
    int verify;
    int dump;
};

// Parseaza argumentele in manager_args
int parse_args(int argc, char** argv, struct manager_args* ma){
    struct option long_opts[] = {
        {"root", required_argument, 0, 'r'},
        {"workers", required_argument, 0, 'w'},
        {"ipc", required_argument, 0, 'i'},
        {"db", required_argument, 0, 'd'},
        {"max-depth", required_argument, 0, 'm'},
        {"simulate-work-ms", required_argument, 0, 's'},
        {"verify", no_argument, 0, 'v'},
        {"dump", no_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    // Parsam optiunile
    int opt;
    while ((opt = getopt_long(argc, argv, "r:w:i:d:m:s:vp", long_opts, NULL)) != -1){
        switch(opt){
        case 'r':
            if(ma->verify == 1 || ma->dump == 1){
                fprintf(stderr, "--root nu poate fi rulat in DB_mode! (nu trebuie sa existe --verify sau --dump)\n");
                return -1;
            }
            ma->root = optarg;
            ma->db_mode = 0;    // Dezactivam modul DB (root este explicitat)
            break;

        case 'w':
            ma->workers = atoi(optarg);
            break;

        case 'i':
            ma->ipc_path = optarg;
            break;

        case 'd':
            ma->db_path = optarg;
            break;

        case 'm':
            ma->max_depth = atoi(optarg);
            break;

        case 's':
            ma->simulate_work_ms = atoi(optarg);
            break;

        case 'v':
            if(ma->db_mode == 1){
                ma->verify = 1;
            }
            else{
                fprintf(stderr, "--verify poate fi rulat doar in DB mode! (nu trebuie sa existe --root)\n");
                return -1;
            }
            break;

        case 'p':
            if(ma->db_mode == 1){
                ma->dump = 1;
            }
            else{
                fprintf(stderr, "--dump poate fi rulat doar in DB mode! (nu trebuie sa existe --root)\n");
                return -1;
            }
            break;

        default:
            fprintf(stderr, "Folosire: %s --root <dir> --workers <N> \n"
                            "[--ipc <path>] [--db <path>] [--max-depth <D>] [--simulate-work-ms] <ms>\n", argv[0]);
            return -1;
        }
    }

    return 0;
}

// Verifica magic, versiunea, dimensiunea si consistenta numarului de records din baza de date
// si daca baza de date este completa
// Returneaza 1 in caz de invalidare, sau 0 in caz de sucess
int verify(const char* db_path){
    int fd = open(db_path, O_RDONLY);
    if(fd == -1){
        perror("open in verify");
        return 1;
    } 

    // Initializeaza campurile
    struct header_db* hd = mmap(
        NULL,
        sizeof(struct header_db),
        PROT_READ,
        MAP_SHARED,
        fd,
        0
    );

    close(fd);
    if(hd == MAP_FAILED){
        perror("mmap in verify");
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    // Verifica magic
    if(strcmp(hd->magic, "INV4") != 0){
        fprintf(stderr, "magic invalid!\n");
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    // Verifica versiunea
    if(hd->version != VERSION){
        fprintf(stderr, "versiune invalida!\n");
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    // Verifica daca este completa
    if(hd->complete != 1){
        fprintf(stderr, "baza de date incompleta!\n");
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    // Verifica dimensiunea (nu poate fi mai mic decat dimensiunea headerului)
    struct stat st;
    if(stat(db_path, &st) != 0){
        perror("stat in verify");
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    if(st.st_size < sizeof(struct header_db)){
        fprintf(stderr, "dimensiune minima prea mica!\n");
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    // Verifica consistenta numarului de records
    // Insumam numarul de fisiere emise de workeri si verificam daca
    // este egal cu numarul total de records
    int records_count = 0;
    struct worker_stats wstats;

    int fd_db = open(db_path, O_RDONLY);
    if(lseek(fd_db, hd->first_stat_offset, SEEK_SET) == -1){
        perror("lseek in verify");
        munmap(hd, sizeof(struct header_db));
        close(fd_db);
        return 1;
    }

    int bytes_read;
    while((bytes_read = read(fd_db, &wstats, sizeof(struct worker_stats))) > 0){
        records_count += wstats.files_emitted;
    }

    if(bytes_read == -1){
        perror("read in verify");
        munmap(hd, sizeof(struct header_db));
        close(fd_db);
        return 1;
    }

    if(records_count != hd->file_record_count){
        fprintf(stderr, "numar incosistent de records date de statistici!\n");
        munmap(hd, sizeof(struct header_db));
        close(fd_db);
        return 1;
    }

    // Parcurgem baza de date si aflam numarul total de inregistrari inserate
    // Daca este diferit fata de record_count, atunci avem esec
    int left_pos = hd->first_record_offset;
    int right_pos = hd->first_stat_offset;
    int offset;
    int count = 0;

    while(sizeof(int) == (bytes_read = pread(fd_db, &offset, sizeof(int), left_pos)) && left_pos < right_pos){
        count++;
        left_pos += offset;
    }

    if(left_pos != right_pos || count != hd->file_record_count){
        fprintf(stderr, "numar incosistent de records date de parcurgere!\n");
        munmap(hd, sizeof(struct header_db));
        close(fd_db);
        return 1;
    }

    munmap(hd, sizeof(struct header_db));
    close(fd_db);

    return 0;
}

// Se afiseaza un sumar a informatiilor din baza de date.
// Daca exista o incosistenta se returneaza 1. Altfel, se returneaza 0
int dump(const char* db_path){
    int fd = open(db_path, O_RDONLY);
    if(fd == -1){
        perror("open in dump");
        return 1;
    } 

    // Initializeaza campurile
    struct header_db* hd = mmap(
        NULL,
        sizeof(struct header_db),
        PROT_READ,
        MAP_SHARED,
        fd,
        0
    );

    close(fd);
    if(hd == MAP_FAILED){
        perror("mmap in dump");
        return 1;
    }

    if(strcmp(hd->magic, "INV4") != 0){
        munmap(hd, sizeof(struct header_db));
        return 1;
    }

    printf("magic=%s\n", hd->magic);
    printf("version=%d\n", hd->version);
    printf("complete=%d\n", hd->complete);
    printf("file_record_count=%d\n", hd->file_record_count);
    printf("worker_count=%d\n", hd->worker_count);

    munmap(hd, sizeof(struct header_db));

    return 0;
}

// Valideaza argumentele date ca si input
int validate_args(struct manager_args* ma){
    if(ma->root == NULL && ma->db_mode == 0){
        fprintf(stderr, "--root nu este specificat!\n");
        return -1;
    }

    if(ma->workers <= 0 && ma->db_mode == 0){
        fprintf(stderr, "--workers trebuie sa fie cel putin 1!\n");
        return -1;
    }

    if(ma->workers > CAPACITY && ma->db_mode == 0){
       fprintf(stderr, "--workers nu poate fi mai mare decat CAPACITY: %d!\n", CAPACITY);
       return -1; 
    }
    
    if(ma->workers > 0 && ma->db_mode == 1){
        fprintf(stderr, "nu pot exista --workers in DB mode!\n");
        return -1;
    }

    if(ma->max_depth <= 0){
        fprintf(stderr, "--max-depth trebuie sa fie cel putin 1! \n");
        return -1;
    }

    if(ma->db_mode == 1 && ma->verify == 0 && ma->dump == 0){
        fprintf(stderr, "nu este specificat nici --verify, si nici --dump in DB mode!\n");
        return -1;
    }
    
    if(ma->verify == 1 && ma->dump == 1){
        fprintf(stderr, "nu poti avea --verify si --dump in acelasi timp!\n");
        return -1;
    }

    if(ma->simulate_work_ms < 0){
        fprintf(stderr, "--simulate_work_ms nu poate fi un numar negativ!\n");
        return -1;
    }

    return 0;
}

int main(int argc, char** argv){
    // Argumentele programului fileops_manager
    struct manager_args ma;
    struct shared_data* sd;
    struct header_db* hd;

    ma.root = NULL;
    ma.workers = 0;
    ma.ipc_path = "./data/ipc.mmap";
    ma.db_path = "./data/inventory.db";
    ma.max_depth = 1024;
    ma.simulate_work_ms = 0;
    ma.db_mode = 1;
    ma.verify = 0;
    ma.dump = 0;

    int failed = 0;

    if(parse_args(argc, argv, &ma) == -1){
        fprintf(stderr, "parse_arguments in main manager\n");
        return 1;
    }

    if(validate_args(&ma) == -1){
        fprintf(stderr, "validate_args in main manager\n");
        return 1;
    }
    
    // Modul --verify
    if(ma.verify == 1){
        return verify(ma.db_path);
    }

    // Modul --dump
    if(ma.dump == 1){
        return dump(ma.db_path);
    }
    
    if(start_db(ma.db_path, ma.workers, &hd) == -1){
        fprintf(stderr, "start_db in main manager\n");
        return 1;
    }
    
    if(start_ipc(ma.ipc_path, ma.workers, ma.max_depth, CAPACITY, ma.simulate_work_ms, &sd) == -1){
        fprintf(stderr, "start_ipc in main manager\n");
        return 1;
    };

    // Deschidem baza de date finala in append mode
    int fd_db = open(ma.db_path, O_APPEND | O_WRONLY);
    if(fd_db == -1){
        perror("open in main manager");
        failed = 1;
        goto cleanup_manager;
    }
    
    // Managerul insereaza primul job (dat prin --root)
    struct job_t initial_job;
    initial_job.depth = 0;
    strcpy(initial_job.job_name, ma.root);
    
    if(enqueue_job(&initial_job, &sd) == -1){
        fprintf(stderr, "enqueue_job in main manager\n");
        failed = 1;
        goto cleanup_manager;
    }

    // Initializam workerii
    if(init_workers(ma.workers, ma.ipc_path) == -1){
        fprintf(stderr, "init_workers in main fileops_manager\n");
        failed = 1;
        goto cleanup_manager;
    }
    
    // Preluam rezultatele de la workeri si le punem in baza de date
    // Facem asta pana cand workerii isi inchid activitatea, dupa care preluam datele ramase din coada rezultatelor
    int ret_value = 0;
    struct result res;

    while((ret_value = dequeue_result(&res, &sd)) == 0){
        // Prelucram rezultatul
        if(append_result_db(fd_db, &res, &hd) == -1){
            fprintf(stderr, "append_result_db in main manager\n");
            failed = 1;
            goto cleanup_manager;
        }
        hd->file_record_count++;
    }

    // Asteptam workerii sa isi publice statisticile si sa se termine
    while(wait(NULL) > 0){
    }

    // Publicam rezultatele ramase din coada
    if(ret_value == 1){
        while(sd->rc.head_pos != sd->rc.tail_pos){
            // Prelucram rezultatele ramase
            
            res = sd->rc.results[sd->rc.head_pos++];
            if(append_result_db(fd_db, &res, &hd) == -1){
                fprintf(stderr, "append_result_db in main manager\n");
                failed = 1;
                goto cleanup_manager;
            }
            
            hd->file_record_count++;
            sd->rc.head_pos %= CAPACITY;
        }
    }

    long total_files = 0;
    long total_jobs = 0;

    // Afisam si punem in baza de date statisticile postate de workeri
    struct worker_stats wstats;
    while(sd->ws.count > 0){
        if(pop_worker_stats(&wstats, &sd) == -1){
            fprintf(stderr, "pop_worker_stats in main fileops_manager\n");
            failed = 1;
            goto cleanup_manager;
        }

        printf("Worker ID: %d\n", wstats.worker_id);
        printf("Worker PID: %d\n", wstats.pid);
        printf("Exit status: %d\n", wstats.exit_status);
        printf("Jobs processed: %d\n", wstats.jobs_processed);
        printf("Files emitted: %d\n", wstats.files_emitted);
        printf("Bytes emitted: %llu\n", wstats.bytes_emitted);
        printf("wall_time_ms: %llu\n", wstats.wall_time_ms);
        printf("user_cpu_us: %llu\n", wstats.user_cpu_us);
        printf("sys_cpu_us: %llu\n\n", wstats.sys_cpu_us);

        total_files += wstats.files_emitted;
        total_jobs += wstats.jobs_processed;

        if(append_stats_db(fd_db, &wstats, &hd) == -1){
            fprintf(stderr, "append_stats_db in main fileops_manager\n");
            failed = 1;
            goto cleanup_manager;
        }
    }

    printf("Fisiere: %d\n", total_files);
    printf("Joburi : %d\n", total_jobs);
    
    // Marcam baza de date ca si "completata"
    hd->complete = 1;

    cleanup_manager:

    munmap(hd, sizeof(struct header_db));
    munmap(sd, sizeof(struct shared_data));
    close(fd_db);

    if(failed == 1){
        return 1;
    }
    return 0;
}
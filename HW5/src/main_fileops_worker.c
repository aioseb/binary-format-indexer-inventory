#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include "../include/sha256.h"
#include "../include/inv_ipc.h"

volatile sig_atomic_t flag_sigterm = 0;

static void handler_sigterm(int sig){
    flag_sigterm = 1;
}

struct worker_args{
    int worker_id;
    char* ipc_path;
    int control_fd;
};

// Functii specifice statisticilor workerului

// Calculeaza diferenta in milisecunde dintre doua momente de timp
long long diff_ms(struct timespec start, struct timespec end){
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

// Calculeaza in microsecunde un timeval
long long timeval_to_us(struct timeval tv){
    return tv.tv_sec * 1000000 + tv.tv_usec; 
}

// Trimite un mesaj prin pipe
int send_msg(int fd, const char* msg){
    if(fd < 0){
        fprintf(stderr, "fd negativ in send_msg\n");
        return 1;
    }

    int len = strlen(msg);
    if(len != write(fd, msg, len)){
        perror("scriere mesaj partial in pipe");
        return 1;
    }

    return 0;
}

// Parseaza argumentele in worker_args
int parse_args(int argc, char** argv, struct worker_args* wa){
    struct option long_opts[] = {
        {"worker-id", required_argument, 0, 'w'},
        {"ipc", required_argument, 0, 'i'},
        {"control-fd", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    // Parsam optiunile
    int opt;
    while ((opt = getopt_long(argc, argv, "w:i:c:", long_opts, NULL)) != -1){
        switch(opt){

        case 'w':
            wa->worker_id = atoi(optarg);
            break;

        case 'i':
            wa->ipc_path = optarg;
            break;

        case 'c':
            wa->control_fd = atoi(optarg);
            break;

        default:
            fprintf(stderr, "Folosire: %s --worker-id <id> --ipc <path>\n", argv[0]);
            return -1;
        }
    }

    return 0;
}

// Valideaza argumentele date ca si input
int validate_args(struct worker_args* wa){
    if(wa->ipc_path == NULL){
        fprintf(stderr, "--ipc nu a fost specificat!\n");
        return -1;
    }

    if(wa->worker_id == -1){
        fprintf(stderr, "--worker-id nu a fost specificat!\n");
        return -1;
    }

    return 0;
}


int main(int argc, char** argv){
    // Cronometram workerul curent
    struct timespec wall_start, wall_end;
    struct rusage usage_start, usage_end;

    clock_gettime(CLOCK_MONOTONIC, &wall_start);
    getrusage(RUSAGE_SELF, &usage_start);

    struct worker_args wa;
    wa.worker_id = -1;
    wa.ipc_path = NULL;
    wa.control_fd = -1;

    // Setam handlerul
    signal(SIGTERM, handler_sigterm);
    signal(SIGINT, handler_sigterm);

    if(parse_args(argc, argv, &wa) == -1){
        fprintf(stderr, "parge_args in worker main\n");
        return 1;
    }

    if(validate_args(&wa) == -1){
        fprintf(stderr, "validate_args in worker main\n");
        return 1;
    }
    
    // Initializam statisticile workerului
    struct worker_stats ws;
    ws.worker_id = wa.worker_id;
    ws.pid = getpid();
    ws.exit_status = 0;
    ws.jobs_processed = 0;
    ws.files_emitted = 0;
    ws.bytes_emitted = 0;
    ws.wall_time_ms = 0;
    ws.user_cpu_us = 0;
    ws.sys_cpu_us = 0;

    int failed = 0;
    int exiting_sent = 0; // S-a trimis deja WORKER_EXITING?
    char msgbuf[256];   // Bufferul pentru mesaj

    // printf("Am pornit workerul cu ID-ul %d pentru IPC-ul %s\n", wa.worker_id, wa.ipc_path);

    // Cat timp exista joburi active sau joburi in coada
    // - preia un job
    // - parcurge-l
    // - pune in coada joburilor subdirectoarele
    // - pune in canalele rezultat metadatele fisierelor regulate

    struct shared_data* sd;
    if(get_mapped_data(wa.ipc_path, &sd) == -1){
        fprintf(stderr, "get_mapped_data in fileops_worker main\n");

        snprintf(msgbuf, sizeof(msgbuf),
                 "T5MSG type=ERROR worker_id=%d errno=%d where=get_mapped_data\n",
                wa.worker_id, errno);
        send_msg(wa.control_fd, msgbuf);

        failed = 1;
        goto cleanup;
    }


    struct job_t job;
    struct job_t temp_job;

    struct stat st;
    struct result res;
    char resolved_path[PATH_MAX];

    while(1){        
        if(sd->hdr.simulate_work_ms != 0){
            usleep(sd->hdr.simulate_work_ms * 1000);
        }

        // Verificam daca workerul a primit SIGTERM; nu mai preluam joburi
        if(flag_sigterm == 1){
            // Shutdown gratios
            snprintf(msgbuf, sizeof(msgbuf),
                 "T5MSG type=WORKER_EXITING worker_id=%d reason=shutdown\n",
                wa.worker_id);
            send_msg(wa.control_fd, msgbuf);
            exiting_sent = 1;

            break;
        }

        // Workerii au terminat in mod normal
        if(dequeue_job(&job, &sd) == 1){
            snprintf(msgbuf, sizeof(msgbuf),
                 "T5MSG type=WORKER_EXITING worker_id=%d reason=normal\n",
                wa.worker_id);
            send_msg(wa.control_fd, msgbuf);
            exiting_sent = 1;

            break;
        }
        
        sem_wait(&sd->jq.job_mutex);
        sd->hdr.active_jobs++;
        sem_post(&sd->jq.job_mutex);
        
        // Procesam jobul curent
        DIR* dir = opendir(job.job_name);
        if(!dir) {
            perror("opendir in fileops_worker main");
            failed = 1;

            snprintf(msgbuf, sizeof(msgbuf),
                 "T5MSG type=ERROR worker_id=%d errno=%d where=opendir\n",
                wa.worker_id, errno);
            send_msg(wa.control_fd, msgbuf);

            goto cleanup;
        }
        
        struct dirent* entry;
        while((entry = readdir(dir)) != NULL && exiting_sent == 0){
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;   // Sarim peste . si ..
            }
            snprintf(resolved_path, PATH_MAX, "%s/%s", job.job_name, entry->d_name);

            if(lstat(resolved_path, &st) == -1){
                perror("lstat in fileops_worker main");
                closedir(dir);
                failed = 1;

                snprintf(msgbuf, sizeof(msgbuf),
                        "T5MSG type=ERROR worker_id=%d errno=%d where=lstat\n",
                        wa.worker_id, errno);
                send_msg(wa.control_fd, msgbuf);
                
                goto cleanup;
            }

            // Punem subdirectoarele gasite in coada de joburi
            if(S_ISDIR(st.st_mode) && job.depth < sd->hdr.max_depth){
                strcpy(temp_job.job_name, resolved_path);
                temp_job.depth = job.depth + 1;

                if(enqueue_job(&temp_job, &sd) == -1){
                    fprintf(stderr, "enqueue_job in fileops_worker main\n");
                    failed = 1;

                    snprintf(msgbuf, sizeof(msgbuf),
                        "T5MSG type=ERROR worker_id=%d errno=%d where=enqueue_job\n",
                        wa.worker_id, errno);
                    send_msg(wa.control_fd, msgbuf);

                    goto cleanup;
                }
                
                // printf("id: %d, path: %s, depth: %d\n", wa.worker_id, temp_job.job_name, temp_job.depth);
            }
            else if(S_ISREG(st.st_mode) && job.depth < sd->hdr.max_depth){
                // printf("id: %d, path: %s, depth: %d\n", wa.worker_id, resolved_path, temp_job.depth);
                strcpy(res.abs_path, resolved_path);
                res.size = st.st_size;
                res.mtime = st.st_mtime;
                res.mode = st.st_mode;
                res.uid = st.st_uid;
                res.gid = st.st_gid;

                // Se calculeaza SHA256 pentru fiecare fisier regulat
                int fd = open(resolved_path, O_RDONLY);
                if(fd == -1){
                    perror("open in main fileops_worker");
                    failed = 1;

                    snprintf(msgbuf, sizeof(msgbuf),
                        "T5MSG type=ERROR worker_id=%d errno=%d where=open_file\n",
                        wa.worker_id, errno);
                    send_msg(wa.control_fd, msgbuf);

                    goto cleanup;
                }

                SHA256_CTX sha256;
                sha256_init(&sha256);

                unsigned char buffer[4096];
                size_t bytes_read;

                while((bytes_read = read(fd, buffer, 4096)) > 0){
                    sha256_update(&sha256, buffer, bytes_read);
                }

                close(fd);

                if(bytes_read == -1){
                    perror("read in main fileops_worker");
                    failed = 1;

                    snprintf(msgbuf, sizeof(msgbuf),
                        "T5MSG type=ERROR worker_id=%d errno=%d where=read_file\n",
                        wa.worker_id, errno);
                    send_msg(wa.control_fd, msgbuf);

                    goto cleanup;
                }

                unsigned char hash[SHA256_BLOCK_SIZE];
                sha256_final(&sha256, hash);
                memcpy(res.sha, hash, SHA256_BLOCK_SIZE);

                if(enqueue_result(&res, &sd) == -1){
                    fprintf(stderr, "enqueue_result in fileops_worker main\n");
                    failed = 1;

                    snprintf(msgbuf, sizeof(msgbuf),
                        "T5MSG type=ERROR worker_id=%d errno=%d where=enqueue_result\n",
                        wa.worker_id, errno);
                    send_msg(wa.control_fd, msgbuf);

                    goto cleanup;
                }

                // Incrementam numarul de fisiere procesate si dimensiunea
                ws.files_emitted++;
                ws.bytes_emitted += st.st_size;
            }
        }

        closedir(dir);
                
        // Incrementam numarul de joburi procesate
        ws.jobs_processed++;

        // Pentru fiecare job procesat transmitem un mesaj JOB_DONE
        snprintf(msgbuf, sizeof(msgbuf),
            "T5MSG type=JOB_DONE worker_id=%d jobs=%d"
            " files=%d bytes=%lld\n",
            wa.worker_id, ws.jobs_processed,
            ws.files_emitted, ws.bytes_emitted);
        send_msg(wa.control_fd, msgbuf);


        // Decrementam numarul de joburi active
        sem_wait(&sd->jq.job_mutex);
        sd->hdr.active_jobs--;
        
        // Workerii termina daca ultimul worker nu mai are ce sa faca
        if(sd->hdr.active_jobs == 0 && sd->hdr.queued_jobs == 0){
            sd->hdr.active = 'E';
            sem_post(&sd->rc.result_full);  // Atentionam managerul ca workerii si-au terminat activitatea
            for(int i = 0; i < sd->hdr.workers; i++){
                sem_post(&sd->jq.job_full); // Ii atentionam pe cei care vor sa citeasca o valoare din coada joburilor
            }
        }
        sem_post(&sd->jq.job_mutex);
    }

    cleanup:
    if(failed == 1){
        ws.exit_status = 1;

        // Decrementam numarul de joburi active
        sem_wait(&sd->jq.job_mutex);
        sd->hdr.active_jobs--;
        
        // Workerii termina daca ultimul worker nu mai are ce sa faca
        // In cazul in care se returneaza o eroare pot sa mai ramana joburi neprocesate in coada.
        if(sd->hdr.active_jobs == 0){
            sd->hdr.active = 'E';
            sem_post(&sd->rc.result_full);  // Atentionam managerul ca workerii si-au terminat activitatea
            for(int i = 0; i < sd->hdr.workers; i++){
                sem_post(&sd->jq.job_full); // Ii atentionam pe cei care vor sa citeasca o valoare din coada joburilor
            }
        }

        sem_post(&sd->jq.job_mutex);
    }

    // Transmite mesajul WORKER_EXITING daca nu a fost trimis deja
    if(exiting_sent == 0){
        const char* reason = failed ? "error" : "normal";
        snprintf(msgbuf, sizeof(msgbuf),
                 "T5MSG type=WORKER_EXITING worker_id=%d reason=%s\n",
                 wa.worker_id, reason);
        send_msg(wa.control_fd, msgbuf);

    }

    // Calculam intervalele workerului respectiv
    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    getrusage(RUSAGE_SELF, &usage_end);

    ws.wall_time_ms = diff_ms(wall_start, wall_end);
    ws.user_cpu_us = timeval_to_us(usage_end.ru_utime) - timeval_to_us(usage_start.ru_utime);
    ws.sys_cpu_us = timeval_to_us(usage_end.ru_stime) - timeval_to_us(usage_start.ru_stime);

    // Publicam statisticile workerului
    if(push_worker_stats(&ws, &sd) == -1){
        fprintf(stderr, "push_worker_stats in main fileops_worker");
        ws.exit_status = 1;
        return 1;
    }

    // printf("Am oprit workerul cu ID-ul %d\n", wa.worker_id);
    munmap(sd, sizeof(struct shared_data));
    return failed;
}
#include <semaphore.h>
#include <string.h>

#define PATH_MAX 4096
#define CAPACITY 2048
#define VERSION 100

// Structura headerului
struct header{
    char magic[5];
    int version;
    int max_depth;
    int workers;
    int simulate_work_ms;
    int capacity;
    char active;
    int queued_jobs;
    int active_jobs;
    int queued_results;
};

// Structura headerului din DB
struct header_db{
    char magic[5];
    int version;
    int complete;
    int file_record_count;
    int worker_count;
    int first_record_offset;
    int first_stat_offset;
};

// Structura unui job
struct job_t{
    int depth;
    char job_name[PATH_MAX];
};

// Structura cozii pentru joburi
struct job_queue{
    int capacity;
    int head_pos;
    int tail_pos;

    sem_t job_empty;
    sem_t job_full;
    sem_t job_mutex;

    struct job_t jobs[CAPACITY];
};

// Structura unui rezultat
struct result{
    char abs_path[PATH_MAX];
    int size;
    int mtime;
    int mode;
    int uid;
    int gid;
    unsigned char sha[32];
};

// Structura canalelor pentru rezultate
struct result_channels{
    int capacity;
    int head_pos;
    int tail_pos;

    sem_t result_empty;
    sem_t result_full;
    sem_t result_mutex;

    struct result results[CAPACITY];
};

// Structura statisticilor returnate de un worker
struct worker_stats{
    int worker_id;
    int pid;
    int exit_status;
    int jobs_processed;
    int files_emitted;
    long long bytes_emitted;
    long long wall_time_ms;
    long long user_cpu_us;
    long long sys_cpu_us;
};

// Structura stivei care tine statisticile workerilor
struct worker_stack{
    int count;
    sem_t worker_stat_mutex;
    struct worker_stats wstats[CAPACITY];
};

// Structura informatiei partajate al IPC-ului
struct shared_data{
    struct header hdr;
    struct job_queue jq;
    struct result_channels rc;
    struct worker_stack ws;
};

// Stiva dinamica de joburi
struct dynamic_stack_jobs{
    struct job_t* jobs;
    int capacity;
    int count;
};

int start_ipc(const char* path, int workers, int max_depth, int capacity, int simulate_work_ms, struct shared_data** sd);
int enqueue_job(const struct job_t* job, struct shared_data** sd);
int dequeue_job(struct job_t* job, struct shared_data** sd);
int enqueue_result(const struct result* res, struct shared_data** sd);
int dequeue_result(struct result* res, struct shared_data** sd);
int push_worker_stats(const struct worker_stats* wstats, struct shared_data** sd);
int pop_worker_stats(struct worker_stats* wstats, struct shared_data** sd);
int get_mapped_data(const char* ipc_path, struct shared_data** sd);

int init_workers(int workers, const char* ipc);
int init_shared_data(struct shared_data* sd, int workers, int max_depth, int capacity, int simulate_work_ms);
int init_header(struct header* hdr, int workers, int max_depth, int capacity, int simulate_work_ms);
int init_job_queue(struct job_queue* jq);
int init_result_channels(struct result_channels* rc);
int init_worker_stack(struct worker_stack* ws);

int start_db(const char* db_path, int workers, struct header_db** hd);
int append_result_db(int fd_db, struct result* res, struct header_db** hd);
int append_stats_db(int fd_db, struct worker_stats* ws, struct header_db** db);
#include <stdint.h>

#define MAX_ENTRY_SIZE 8192 

// fileops_indexer
#define PATH_MAX 4096
#define TYPE_LEN_MAX 16
#define CHECKSUM_LEN 8
#define FIELD_FILEOPS_COUNT 11

// campuri fileops_indexer
#define IDX_ENTRY_SIZE 0 
#define IDX_PATH_LEN 1
#define IDX_ABS_PATH 2
#define IDX_FILE_TYPE 3
#define IDX_CHECKSUM 4
#define IDX_SIZE_BYTES 5
#define IDX_MTIME 6
#define IDX_HASH 7
#define IDX_DEV 8
#define IDX_INODE 9
#define IDX_NEXT_ENTRY_BUCKET 10

// offset-uri
#define IDX_ENTRY_SIZE_OFFSET 0
#define IDX_PATH_LEN_OFFSET 4
#define IDX_ABS_PATH_OFFSET 8
#define IDX_STEP64_OFFSET sizeof(uint64_t)


// proc_snapshot
#define LEN_CMDLINE_MAX 1024
#define LEN_NAME_MAX 256
#define LEN_STAT_MAX 512
#define FIELD_PROC_COUNT 13

// campuri proc_snapshot
#define PROC_ENTRY_SIZE 0
#define PROC_PID 1
#define PROC_PPID 2
#define PROC_STATE 3
#define PROC_COMM_SIZE 4
#define PROC_COMM 5
#define PROC_CMDLINE_SIZE 6
#define PROC_CMDLINE 7
#define PROC_RSS_SRC_SIZE 8
#define PROC_RSS_SRC 9
#define PROC_RSS 10
#define PROC_CPU_TIME 11
#define PROC_NEXT_ENTRY_BUCKET 12

// offset-uri
#define PROC_ENTRY_SIZE_OFFSET 0
#define PROC_PID_OFFSET 4
#define PROC_PPID_OFFSET 8
#define PROC_STATE_OFFSET 12
#define PROC_COMM_SIZE_OFFSET 13
#define PROC_STEP32_OFFSET sizeof(uint32_t)
#define PROC_STEP64_OFFSET sizeof(uint64_t)


// header
#define VERSION_VALUE 100
#define BUCKET_COUNT 256

#define STATE_OPEN 'O'
#define STATE_SEALED 'S'

#define FILEOPS_MAGIC_ID 0
#define PROC_MAGIC_ID 1

#define FILEOPS_MAGIC "IDX1"
#define PROC_MAGIC "PRC1"

// campuri header
#define MAGIC 0
#define VERSION 1
#define SNAPSHOT_ID 2
#define STATE 3     // 'S' - SEALED ; 'O' - OPEN
#define ACTIVE_WRITERS 4
#define RECORD_COUNT 5
#define FIRST_ENTRY_POS 6 // Offset-ul global al urmatorului entry ce urmeaza a fi introdus
#define BUCKET 7    // Prin intermediul bucket-urilor vertificam daca o regiune este locked
#define BUCKET_HEAD (BUCKET + BUCKET_COUNT)
#define BUCKET_TAIL (BUCKET + 2 * BUCKET_COUNT)

// dimensiuni campuri header
#define MAGIC_SIZE sizeof(uint32_t)
#define VERSION_SIZE sizeof(uint32_t)
#define SNAPSHOT_ID_SIZE sizeof(uint32_t)
#define STATE_SIZE sizeof(char)
#define ACTIVE_WRITERS_SIZE sizeof(uint32_t)
#define RECORD_COUNT_SIZE sizeof(uint32_t)
#define FIRST_ENTRY_POS_SIZE sizeof(uint32_t)
#define BUCKET_SIZE sizeof(char)
#define BUCKET_HEAD_SIZE sizeof(uint32_t)
#define BUCKET_TAIL_SIZE sizeof(uint32_t)

// offset-uri
#define MAGIC_OFFSET 0
#define VERSION_OFFSET 4
#define SNAPSHOT_ID_OFFSET 8
#define STATE_OFFSET 12
#define ACTIVE_WRITERS_OFFSET 13
#define RECORD_COUNT_OFFSET 17
#define FIRST_ENTRY_POS_OFFSET 21
#define BUCKET_OFFSET 25     // Offset-ul primului bucket
#define BUCKET_HEAD_OFFSET (BUCKET_OFFSET + BUCKET_SIZE * BUCKET_COUNT) // Offsetu-ul primului bucket HEAD 
#define BUCKET_TAIL_OFFSET (BUCKET_HEAD_OFFSET + BUCKET_HEAD_SIZE * BUCKET_COUNT)

#define EMPTY_BUCKET 0xFFFFFFFF
#define HEADER_SIZE (BUCKET_TAIL_OFFSET + BUCKET_TAIL_SIZE * BUCKET_COUNT)
#define FIELD_HEADER_COUNT (BUCKET_TAIL + BUCKET_COUNT)
#define FIRST_RECORD HEADER_SIZE

// Folosite pentru parcurgeri
#define NEXT_IS_REGULAR 0 // Parcurge entry-uri regulate
#define NEXT_IS_BUCKET  1 // Parcurge entry-uri care se afla in acelasi BUCKET ca entry-ul curent

struct db_fileops_entry{
    uint32_t entry_size;
    uint32_t path_len;
    char* absolute_path;    // size = path_len in db (null-terminator inclus)
    char type[TYPE_LEN_MAX];
    uint64_t checksum;
    uint64_t size_bytes;
    uint32_t mtime;
    uint64_t hash;
    uint32_t dev;
    uint32_t inode;
    uint32_t next_entry_bucket;
};

struct db_proc_entry{
    uint32_t entry_size;
    uint32_t pid;
    uint32_t ppid;
    uint8_t state;
    uint32_t comm_size;
    const char* comm;       // size = comm_size in db (null-terminator inclus) 
    uint32_t cmdline_size;
    const char* cmdline;    // size = cmdline_size in db (null-terminator inclus)
    uint32_t rss_src_size; 
    const char* rss_src;    // size = rss_src_size in db (null-terminator inclus)
    uint64_t rss;
    uint64_t cpu_time;      // Total CPU time (user + kernel)
    uint32_t next_entry_bucket;
};
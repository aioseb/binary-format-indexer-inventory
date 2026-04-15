#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

int validate_directory(const char* path){
    if(path == NULL){
        fprintf(stderr, "Eroare: directorul sursa nu a fost introdus!\n");
        exit(2);
    }

    // Verificam daca <dir> este un director, daca exista, si daca poate fi citit
    struct stat dir_st;
    if(stat(path, &dir_st) == 0){;
        if(!S_ISDIR(dir_st.st_mode)){
            fprintf(stderr, "Eroare: directorul sursa nu exista sau nu este de tip 'directory'!\n");
            exit(3);
        }
        if(!(dir_st.st_mode & R_OK)){
            fprintf(stderr, "Eroare: nu exista drepturi de citire pentru directorul dat!\n");
            exit(3);
        }
    } else{
        perror("Eroare stat");
        exit(1);
    }

    return 0;
}

int validate_destination(const char* path){
    if(path == NULL){
        fprintf(stderr, "Eroare: destinatia nu a fost introdusa!\n");
        exit(2);
    }

    // Validam destinatia. Daca aceasta nu exista, cream o baza de date noua
    // Ne asiguram ca aceasta poate fi suprascrisa.
    int fd;
    if(-1 == (fd = open(path, O_WRONLY | O_CREAT, 0755))){
        perror("Eroare open");
        exit(1);
    }
    close(fd);

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

    validate_directory(dir);
    validate_destination(dest);

    printf("%s %s\n", dir, dest);
}
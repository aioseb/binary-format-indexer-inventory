# T4 - File Inventory Builder multiproces in C

Tema 4 include un sistem de inventariere printr-un sablon Manager - Workers. Workerii proceseaza joburi si produc metadate a fisierelor regulate, iar Managerul consuma aceste metadate.

## 1. Structura 
Formatul IPC este alcatuit din 4 componente principale
1. Header
2. Job queue
3. Result channels
4. Worker stats stack

## 2. Header
In header sunt prezente urmatoarele campuri:

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| MAGIC | 5 | Valoarea magic al IPC-ului (IPC4) |
| VERSION | 4 | Versiunea folosita |
| MAX_DEPTH | 4 | Adancimea maxima a joburilor procesate |
| SIMULATE_WORK_MS | 4 | Pentru testare: intarziere controlata a procesarii joburilor |
| CAPACITY | 4 | Capacitatea bufferelor circulare |
| ACTIVE | 1 | 'A' daca exista workeri care proceseaza ; 'E' daca nu mai proceseaza niciun worker |
| QUEUED_JOBS | 4 | Numarul de joburi in coada joburilor |
| ACTIVE_JOBS | 4 | Numarul de joburi care sunt in momentul actual procesate |
| QUEUED_RESULTS | 4 | Numarul de rezultate in coada canalelor de rezultate |

## 3. Job queue
Un job queue este un buffer circular de dimensiune fixa `CAPACITY` cu urmatoarele camprui:

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| CAPACITY | 4 | Capacitatea bufferului |
| HEAD_POS | 4 | Pozitia de inceput a cozii |
| TAIL_POS | 4 | Pozitia de final a cozii |
| JOB_EMPTY | sizeof(sem_t) | Semafor care indica cat de gol este bufferul (initializat cu CAPACITY) |
| JOB_FULL | sizeof(sem_t) | Semafor care indica cat de plin este bufferul (initializat cu 0)
| JOB_MUTEX | sizeof(sem_t) | Semafor binar pentru coada de joburi (initializat cu 1) | 
| JOBS[] | sizeof(job_t) * CAPACITY | Vectorul de joburi folosit in implementarea cozii |

O structura `job_t` are urmatoarele campuri

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| DEPTH | 4 | Adancimea jobului |
| JOB_NAME | PATH_MAX | Calea absoluta catre job |

## 4. Results channel
Un result channel este un buffer circular de dimensiune fixa `CAPACITY` cu urmatoarele campuri:

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| CAPACITY | 4 | Capacitatea bufferului |
| HEAD_POS | 4 | Pozitia de inceput a cozii |
| TAIL_POS | 4 | Pozitia de final a cozii |
| RESULT_EMPTY | sizeof(sem_t) | Semafor care indica cat de gol este bufferul (initializat cu CAPACITY) |
| RESULT_FULL | sizeof(sem_t) | Semafor care indica cat de plin este bufferul (initializat cu 0)
| RESULT_MUTEX | sizeof(sem_t) | Semafor binar pentru coada de joburi (initializat cu 1) | 
| RESULTS[] | sizeof(result) * CAPACITY | Vectorul de rezultate folosit in implementarea cozii |

O structura 'result' are urmatoarele campuri

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| ABS_PATH | PATH_MAX | Calea absoluta catre fisierul emis |
| SIZE | 4 | Dimensiunea in bytes a fisierului |
| MTIME | 4 | Momentul modificarii in UNIX timestamp |
| MODE | 4 | Modul fisierului |
| UID | 4 | ID-ul userului caruia apartine fisierului |
| GID | 4 | ID-ul grupului caruia apartine fisierului |
| SHA | 32 | SHA256 checksum asupra continutului |

## 5. Worker stats stack
Un worker stats stack reprezinta o stiva la finalul fisierului IPC in care sunt puse statisticile finale de fiecare worker in parte. Sunt prezente urmatoarele campuri:

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| COUNT | 4 | Numarul de elemente din stiva |
| WORKER_STAT_MUTEX | sizeof(sem_t) | Semafor binar pentru stiva |
| WSTATS[] | sizeof(worker_stats) * CAPACITY | Vectorul de statistici folosit in implementarea stivei |

O structura `worker_stats` are urmatoarele campuri
 
| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| WORKER_ID | 4 | ID-ul workerului asignat de Manager |
| PID | 4 | PID-ul workerului |
| EXIT_STATUS | 4 | Codul de terminare a workerului |
| JOBS_PROCESSED | 4 | Numarul total de joburi procesate de catre worker |
| FILES_EMITTED | 4 | Numarul total de fisiere regulate emise de catre worker |
| BYTES_EMITTED | 4 | Numarul total de octeti emisi de catre worker |
| WALL_TIME_MS | 4 | Timpul efectiv de lucru masurat in milisecunde |
| USR_CPU_US | 4 | Timpul de executie in user_mode masurat in microsecunde |
| SYS_CPU_US | 4 | Timpul de executie in kernel_mode masurat in microsecunde |

## 6. Limite impuse
`PATH_MAX = 4096` - Dimensiunea maxima a unei cai absolute\
`CAPACITY = 2048` - Capacitatea bufferelor\
`0 < WORKER_COUNT <= CAPACITY` - Numarul de workeri nu poate fi negativ, si nici nu poate fi mai mare decat `CAPACITY`

## 7. Sincronizare
De fiecare data atunci cand un worker pune fie in coada joburilor, fie in coada  rezultatelor un element, acesta asteapta pentru un spatiu liber prin folosirea semaforului `job_empty`, respectiv `result_empty`.

Cat timp exista joburi care se proceseaza, workerii care nu au joburi asteapta sa preia un element din coada joburilor prin intermediul semaforului `job_full` care reprezinta cate joburi sunt la un moment dat in coada respectiva. 

De fiecare data cand se insereaza sau se sterge un element dintr-una din cele doua cozi se folosesc mutexurile corespunzatoare (`job_mutex` si `result_mutex`) pentru a asigura un acces la date fara race condition, si se actualizeaza semafoarele ('job_empty' sau 'result_empty', respectiv `job_full` sau `result_full`). 

Cat timp exista rezultate in coada rezultatelor, managerul scrie atomic o inregistrare de dimensiune dinamica in baza de date finala.

In final, daca ultimul worker afla ca nu mai exista nici joburi care se proceseaza activ, si afla ca nu mai exista nici joburi in coada, acesta anunta pe toti ceilalti workeri si managerul sa isi incheie procesul.

Dupa acest lucru, managerul asteapta terminarea executiei workerilor prin `wait()`, timp in care ei isi publica statisticile finale in IPC.

## 8. Backpressure
Daca coada canalelor de rezultate este plina, atunci workerul trebuie sa astepte ca managerul sa consume un record. Workerul stie daca o coada este plina daca semaforul `result_empty` este 0.

Daca managerul consuma un record, atunci acesta incrementeaza semaforul `result_empty` si ofera posibilitatea workerilor de a pune o noua inregistrare, fara sa existe suprascrieri.

Aceeasi strategie este folosita si in cadrul cozii de joburi. Daca coada respectiva este plina, atunci workerul care vrea sa puna un job nou in coada trebuie sa astepte ca alt worker sa preia un job din coada.

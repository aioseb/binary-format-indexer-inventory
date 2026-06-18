# T4 - DB Format
Baza de date din T4 este alcatuita dintr-un header, inregistrari, iar la final statisticile fiecarui worker

## 1. Header
Structura headerului este in felul urmator:

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| MAGIC | 4 | Valoarea magic al DB-ului (INV4) |
| VERSION | 4 | Versiunea DB-ului |
| COMPLETE | 4 | 1 - daca s-a terminat normal ; 0 - daca a fost intrerupt |
| FILE_RECORD_COUNT | 4 | Numarul total de inregistrari din baza de date |
| WORKER_COUNT | 4 | Numarul de workeri care au procesat joburile |
| FIRST_RECORD_OFFSET | 4 | Offsetul primului record din baza de date |
| FIRST_STAT_OFFSET | 4 | Offsetul primului worker_stats din baza de date (precedat de inregistrari) | 

## 2. Inregistrare
Structura unei inregistrari este in felul urmator

| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| ENTRY_SIZE | 4 | Dimensiunea in octeti a inregistrarii curente (folosit pentru parcurgeri)|
| ABS_PATH_LEN | 4 | Lungimea caii absolute ABS_PATH |
| ABS_PATH | ABS_PATH_LEN | Calea absoluta a fisierului |
| SIZE | 4 | Dimensiuneae in bytes a fisierului |
| MTIME | 4 | Momentul modificarii in UNIX Timestamp |
| MODE | 4 | Modul fisierului |
| UID | 4 | ID-ul userului caruia ii detine fisierul |
| GID | 4 | ID-ul grupului caruia ii detine fisierul | 
| SHA | 32 | SHA256 checksum asupra continutului |

## 3. Worker statistics
La finalul DB-ului avem publicate statistici prin structuri `worker_stats`.\
Structura `worker_stats` este aceeasi ca cea descrisa in `/data/MMAP_PROTOCOL.md`:

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

## 4. Limitele alese
`CAPACITY` = `2048`\
`PATH_MAX` = `4096`\
Numarul de statistici publicate < `CAPACITY`\
`WORKER_COUNT` < `CAPACITY`\
`ABS_PATH_LEN` < `PATH_MAX`

## 5. Conditiile de validitate a DB-ului
Pentru ca un DB sa fie valid acesta trebuie sa respecte urmatoarele conditii:
- `MAGIC` = `INV4`
- `VERSION` sa fie compatibil
- `COMPLETE` = 1 (sa fie terminat normal)
- `RECORD_COUNT` sa fie consistent (atat prin parcurgerea recordurilor, cat si prin insumarea fisierelor emise de workeri)
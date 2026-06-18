# T3 - Indexare de fisiere si snapshot de procese

Tema T3 include un format binar pentru baze de date versionate, cu sincronizare. Inregistrarile au dimensiune dinamica si contin offset-uri bine definite.

## 1. Header
Header-ul este de dimensiune fixa si include mai multe campuri ce sunt actualizate pe parcursul inserarii a inregistrarilor.

### Structura:
| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| MAGIC | 4 | Reprezinta tipul bazei de date (IDX1 sau PRC1) |
| VERSION | 4 | Versiunea bazei de date |
| SNAPSHOT_ID | 4 | Identificatorul |
| STATE | 1 | Starea in care se afla (O - OPEN sau S - SEALED) |
| ACTIVE_WRITERS | 4 | Numarul de procese care contribuie in acelasi timp |
| RECORD_COUNT | 4 | Numarul de inregistrari in baza de date |
| FIRST_ENTRY_POS | 4 | Locatia primei inregistrari |
| BUCKET | 256 * 1 | Zona folosita pentru sincronizare in timpul scrierii |
| BUCKET_HEAD | 256 * 4 | Locatiile primelor inregistrari din BUCKET-urile corespunzatoare |
| BUCKET_TAIL | 256 * 4 | Locatiile ultimelor inregistrari din BUCKET-urile corespunzatoare | 

## 2. fileops_indexer
Bazele de date generate cu `fileops_indexer` au `MAGIC = IDX1`.
Include doar fisierele care se afla in directorul specificat prin `--root`, si se salveaza in mod implicit in formatul `./data/index_<SNAPSHOT_ID>.db`, sau explicit prin calea specificata prin `--db`.

### Structura:
| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| ENTRY_SIZE | 4 | Dimensiunea in Bytes a inregistrarii |
| PATH_LEN | 4 | Dimensiunea in Bytes a caii absolute|
| ABS_PATH | PATH_LEN | Calea absoluta (cheia primara) |
| FILE_TYPE | 16 | Tipul fisierului |
| CHECKSUM | 8 | Checksum-ul continutului fisierelor regulate calculat prin FNV-1a hash (0 pentru celelalte tipuri) |
| SIZE_BYTES | 8 | Dimensiunea in Bytes a fisierului regulat |
| MTIME | 4 | Timpul modificarii in UNIX Timestamp |
| HASH | 8 | Hash-ul lui ABS_PATH calculat prin FNV-1a hash (a nu se confunda cu CHECKSUM) |
| BINARY_NAME | 4 + 4 | Numele binar alcatuit din campurile DEV (device) si INODE (inode) |
| NEXT_ENTRY_BUCKET | 4 | Locatia urmatoarei inregistrari din BUCKET-ul corespunzator |

### FNV-1a
Pentru `CHECKSUM` si `HASH` s-a folosit FNV1-a pe 64 biti.

Algoritmul este descris in felul urmator:
```
FNV-1a (64 bits):
    hash = 14695981039346656037
    const prime = 1099511628211

    for each byte in source:
        hash ^= byte
        hash *= prime

    return hash
```

### Limite:
`ENTRY_SIZE` < 8192\
Lungimea lui `FILE_TYPE` < 16 (daca se trece de acest prag, atunci se va trunchia)\
`PATH_LEN` < 4096 (daca se trece de acest prag atunci se va trunchia ABS_PATH)


## 3. proc_snapshot
Bazele de date generate cu `proc_snapshot` au `MAGIC = PRC1`. Include doar procesele capturate la initializare din `/proc`. Daca un proces dispare in timpul scrierii atunci acesta este omis. Se salveaza implicit in formatul `./data/proc_<SNAPSHOT_ID>.db`, sau explicit in calea specificata prin `--db`. 

### Structura:
| Nume | Dimensiune (Bytes) | Descriere |
| ---- | ---------- | --------- |
| ENTRY_SIZE | 4 | Dimensiunea in Bytes a inregistrarii |
| PID | 4 | Process ID-ul procesului |
| PPID | 4 | Parent Process ID-ul procesului |
| STATE | 1 | Starea procesului |
| COMM_SIZE | 4 | Lungimea in Bytes a comm-ului |
| COMM | COMM_SIZE | Comm-ul procesului |
| CMDLINE_SIZE | 4 | Lungimea in Bytes a cmdline-ului |
| CMDLINE | CMDLINE_SIZE | Cmdline-ul procesului |
| RSS_SRC_SIZE | 4 | Lungimea sursei a RSS-ului |
| RSS_SRC | RSS_SRC_SIZE | Sursa din care a fost preluat RSS-ul |
| RSS | 8 | RSS-ul procesului |
| CPU_TIME | 8 | Timpul CPU, acesta fiind suma dintre User Time si Kernel Time |
| NEXT_ENTRY_BUCKET| 4 | Locatia urmatoarei inregistrari din BUCKET-ul corespunzator |

### Limite:
`ENTRY_SIZE` < 8192\
`COMM_SIZE` < 256 (daca se trece de acest prag atunci se va trunchia COMM)\
`CMDLINE_SIZE` < 1024 (daca se trece de acest prag atunci se va trunchia CMDLINE)\
26 < `RSS_SRC_SIZE` < 40 (de obicei `RSS_SRC_SIZE ~ 28`)\
Numarul maxim de PID-uri ce pot fi inserate < 4096 

## 4. Strategia de actualizare
La inceput se initializeaza Header-ul in starea `STATE = OPEN` si `ACTIVE_WRITERS = 1`, iar `VERSION` si `MAGIC` sunt initializate cu valori prestabilite.

Pentru fiecare proces care scrie in baza de date, daca aceasta se afla in `STATE = SEALED` atunci o trunchiaza si o suprascrie, sau daca `STATE = OPEN` atunci incrementeaza campul `ACTIVE_WRITERS` prin access exclusiv cu `fcntl 2`. 

La inchidere se decrementeaza campul `ACTIVE_WRITERS` prin access exclusiv, iar daca `ACTIVER_WRITERS = 0` atunci ultimul proces care inchide baza de date o marcheaza  ca fiind `STATE = SEALED`. In plus, se adauga la `RECORD_COUNT` prin acces exclusiv numarul de inregistrari adaugate cu succes de catre procesul individual.

Inainte de inserarea unei inregistrari se calculeaza `hash`-ul acesteia. Pentru fisiere avem `hash = HASH` (a nu se confunda cu `CHECKSUM`), iar pentru procese `hash = PID`.

Prin intermediul `hash`-ului se determina `BUCKET`-ul corespunzator in care se afla inregistrarea respectiva: `bucket_entry = hash % 256`, unde 256 este numarul de `BUCKET`-uri.

Dupa acest calcul se pune lacat peste `BUCKET`-ul `bucket_entry` folosind `fcntl 2` pentru actualizarea exclusiva a regiunilor asociate intrarii. 

Se verifica existenta duplicatelor prin comparatia cheilor primare si parcurgerea liniara doar a inregistrarilor care se afla in `BUCKET`-ul `bucket_entry` prin `NEXT_ENTRY_BUCKET`. 

Daca nu s-a gasit niciun duplicat atunci se intializeaza `BUCKET_HEAD`-ul si se actualizeaza `BUCKET_TAIL`-ul corespunzator cu pozitia inregistrarii care apoi este inserata prin `APPEND`, si se incrementeaza un contor a inregistrarilor adaugate cu succes la nivel de proces.

Dupa inserare se ia lacatul de pe `BUCKET`-ul `bucket_entry` si se continua parcurgerea recursiva.

## 5. Conditiile minime pentru o baza de date valida
Pentru ca o baza de date sa fie valida aceasta trebuie sa fie creata cu succes prin parcurgere recursiva a tuturor fisierelor din directorul specificat prin `--root`/ prin capturarea proceselor din `/proc`, in paralel, fara sa existe duplicate.

Dupa terminarea tuturor proceselor baza de date trebuie sa se afle in starea `STATE = SEALED`.

## 6. Regulile de comparare folosite de db_diff
Doua inregistrari sunt considerate "egale" daca au aceeasi cheie primara si aceleasi campuri sau "diferite" daca difera prin cheia primara..

a) Doua inregistrari generate de `fileops_indexer` sunt considerate "modificate" daca au aceeasi cheie primara (`ABS_PATH`), dar difera prin cel putin una dintre urmatoarele campuri:
- `MTIME`
- `CHECKSUM`
- `SIZE_BYTES`
- `FILE_TYPE`\
Mentiune: in cazul fisierelor de tip `symlink` comparatia cu target-ul `symlink`-ului este facuta implicit prin comparatia cu `ABS_PATH`, deoarece se respecta urmatorul format pentru caile absolute pentru `TARGET = symlink`: `"ABS_SYMLINK_PATH"->"ABS_TARGET_PATH"`.

b) Doua inregistrari generate de `proc_snapshot` sunt considerate "modificate semnificativ" daca au aceeasi cheie primara (`PID`), dar difera prin cel putin una dintre urmatoarele campuri:
- `COMM`
- `CMDLINE`
- Diferenta absoluta dintre `RSS`-uri trece de pragul de 1 MB.

Pentru ca sa fie posibila comparatia dintre doua baze de date acestea trebuie sa aiba acelasi `MAGIC` si `VERSION`, iar `SNAPSHOT_ID` trebuie sa fie diferit.

O inregistrare este: 
- `"Creat"` daca ea este in "db nou", dar nu este in "db vechi"
- `"Sters"` daca ea este in "db vechi", dar nu mai este in "db nou"
- `"Modificat"`: daca ea este in "db vechi", dar se regaseste in "db nou" cu campurile modificate (precizate mai sus).

Mentiune: Pentru a face cat mai putine operatii se face comparatia doar intre inregistrarile care se afla in acelasi `BUCKET`.

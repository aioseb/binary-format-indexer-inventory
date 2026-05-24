# T5 - Control Plane

## 1. Pipe
Transmiterea intre mesaje se face printr-un pipe anonim (fara nume pe sistemul de fisiere).

Workerii scriu in pipe in timp ce managerul citeste mesajele transmise de catre workeri.

## 2. Format T5MSG
In functie de tipul sau, T5MSG are mai multe formate:
- T5MSG type=JOB_DONE worker_id=\<id> jobs=\<no_of_jobs>
files=\<files_emitted> bytes=\<total_size> (transmis de worker atunci cand termina de procesat un job)

- T5MSG type=WORKER_EXITING worker_id=\<id> reason=\<normal|shutdown> (transmis atunci cand un worker isi opreste activitatea)

- T5MSG type=ERROR worker_id=\<id> errno=<error_number> where=\<command_that_returned_error> (transmis atunci cand un worker are o eroare)

## 3. Format STATUS
- STATUS queued_jobs=<n> active_jobs=<n> files=<n> bytes=<n> workers_alive=<n> complete=\<0|1|2>

complete=0 - DB-ul nu a fost initializat

complete=1 - DB-ul a fost completat partial

complete=2 - DB-ul a fost completat in totalitate

## 4. Semnale
### Manager
- SIGUSR1 - Se afiseaza STATUS
- SIGINT / SIGTERM - Initiaza shutdown si transmite SIGTERM workerilor, iar dupa timeout le transmite SIGKILL workerilor care nu si-au terminat procesarea
- SIGCHLD - Elimina workerii zombie

### Worker
- SIGTERM - Nu mai accpeta joburi noi si incearca sa transmita WORKER_EXITING

## 5. Shutdown
La initierea shutdown-ului, managerul primeste SIGINT / SIGTERM, iar acesta transmite tuturor workerilor semnalul SIGTERM.

Dupa o anumita perioada de timp (timeout, implicit 8 secunde), managerul transmite workerilor semnalul SIGKILL pentru a forta inchiderea acestora, chiar daca nu si-au terminat procesarea.

In acest caz workerii nu mai preiau joburi noi si incearca sa proceseze intregul job curent. Daca le ia prea mult timp sa proceseze jobul, acestia sunt eliminati prin SIGKILL fara sa corupa IPC-ul.

In final, managerul publica rezultatele ramase din JOB_QUEUE in DB-ul final.
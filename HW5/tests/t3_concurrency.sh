# Testul pentru T3 se considera finalizat cu succes daca:
# 1. La rularea concurenta a comenzilor "fileops_indexer" si "proc_snapshot"
#    s-au creat cu succes fisierele corespunzatoare, fara chei primare duplicate (cai absolute si PID-uri).
#
# 2. Snapshot-urile s-au finalizat cu starea SEALED.
#
# 3. Generarea cu succes a rapoartelor ./reports/T3_filediff.txt si ./report/T3_procdiff.txt
#
# 4. Continuturile din ./reports/T3_filediff.txt trebuie sa contina:
#  - "Creat: /.*/tmp/created_file.txt"
#  - "Sters: /.*/tmp/removed_file.txt"
#  - "Modificat: /.*/tmp/modified_file.txt"
#    Aceste fisiere vor fi sterse dupa rularea diff-ului peste fisiere
#
# 5. Continuturile din ./reports/T3_procdiff.txt trebuie sa contina cel putin un proces,
#    fie el Modificat, Sters sau Creat, cu formatul "[Modificat|Sters|Creat]: PROC = <pid>"

processes_count=5   # Numarul de procese ce vor fi rulate in paralel
state_offset=12

# Testare generare concurenta fileops_indexer ---------------------------------------
declare -a pid_list_fileops
fileops_out="./data/index.db"

for ((i=0; i<processes_count; i++)); do
    ./tools/fileops.sh run -- fileops_indexer --root . --db "$fileops_out" & 
    pid=$!
    pid_list_fileops+=($pid)
done

failed=0
for pid in "${pid_list_fileops[@]}"; do
    wait $pid
    exit_code=$?

    if [[ $exit_code -ne 0 ]]; then
        failed=1  # Una sau mai multe instante au esuat in timpul concurentei. Nu toate au putut contribui.
    fi
done

if [[ $failed -eq 1 ]]; then
    exit 1
fi

# Verificam daca snapshot-ul a fost finalizat cu starea SEALED
state=$(hexdump -s $state_offset -n 1 -e '1/1 "%c"' $fileops_out)
if [[ $state != "S" ]]; then
    exit 2
fi

# Cautam duplicate
strings $fileops_out | sort | uniq -d | grep -q -e "^/[[:print:]]*$"
if [[ $? -eq 0 ]]; then
    exit 3  # grep intoarce 0 atunci cand acesta gaseste un pattern. In cazul asta, s-a gasit o duplicata
fi

sleep 1 # Pentru ID diferit

# Testare generare concurenta proc_snapshot ------------------------------------------
declare -a pid_list_proc
proc_out="./data/proc.db"

for ((i=0; i<processes_count; i++)); do
    ./tools/fileops.sh run -- proc_snapshot --root . --db "$proc_out" &
    pid=$!
    pid_list_proc+=($pid)
done

failed=0
for pid in "${pid_list_proc[@]}"; do
    wait $pid
    exit_code=$?

    if [[ $exit_code -ne 0 ]]; then
        failed=1  # Una sau mai multe instante au esuat in timpul concurentei. Nu toate au putut contribui.
    fi
done

if [[ $failed -eq 1 ]]; then
    exit 1
fi

state=$(hexdump -s $state_offset -n 1 -e '1/1 "%c"' $proc_out)
if [[ $state != "S" ]]; then
    exit 2
fi

# Cautam duplicate
strings $proc_out | sort | uniq -d | grep -q "^/proc/[0-9]*/stat(field 24)$"
if [[ $? -eq 0 ]]; then
    exit 3  # grep intoarce 0 atunci cand acesta gaseste un pattern. In cazul asta, s-a gasit o duplicata
fi

sleep 1

# Testare generare raporate diff ------------------------------------------------------
fileops_old="./data/index_old.db"
fileops_new="./data/index_new.db"

proc_old="./data/proc_old.db"
proc_new="./data/proc_new.db"

diff_fileops_out="./reports/T3_filediff.txt"
diff_proc_out="./reports/T3_procdiff.txt"

# Pentru fileops_indexer cream niste fisiere temporare in ./tmp
removed_file="./tmp/removed_file.txt"
modified_file="./tmp/modified_file.txt"
created_file="./tmp/created_file.txt"

touch ./tmp/removed_file.txt
touch ./tmp/modified_file.txt

./tools/fileops.sh run -- fileops_indexer --root . --db $fileops_old
if [[ $? -ne 0 ]]; then
    exit 1  # Eroare aparuta in fileops_indexer
fi

sleep 1 # Sleep pentru ca fisierele sa aiba ID-uri diferite

rm $removed_file
echo "Modificare" > $modified_file
touch $created_file

./tools/fileops.sh run -- fileops_indexer --root . --db $fileops_new
if [[ $? -ne 0 ]]; then
    rm $modified_file
    rm $created_file
    exit 1  # Eroare aparuta in fileops_indexer
fi

rm $modified_file
rm $created_file

./tools/fileops.sh run -- db_diff --old $fileops_old --new $fileops_new --out $diff_fileops_out
if [[ $? -ne 0 ]]; then
    rm $modified_file
    rm $created_file
    exit 1  # Eroare aparuta in fileops_indexer
fi

# Verificam existenta fisierului
if [[ ! -e "$diff_fileops_out" ]]; then
    exit 2
fi

# Verificam continutul
grep -q "Creat: /.*/created_file.txt" $diff_fileops_out 
if [[ $? -ne 0 ]]; then
    exit 2
fi

grep -q "Sters: /.*/tmp/removed_file.txt" $diff_fileops_out
if [[ $? -ne 0 ]]; then
    exit 2
fi

grep -q "Modificat: /.*/tmp/modified_file.txt" $diff_fileops_out
if [[ $? -ne 0 ]]; then
    exit 2
fi

sleep 1

# Pentru proc_snapshot vom astepta 2 secunde intre cele doua snapshot-uri
./tools/fileops.sh run -- proc_snapshot --root . --db $proc_old
if [[ $? -ne 0 ]]; then
    exit 1  # Eroare aparuta in proc_snapshot
fi

sleep 2

./tools/fileops.sh run -- proc_snapshot --root . --db $proc_new
if [[ $? -ne 0 ]]; then
    exit 1  # Eroare aparuta in proc_snapshot
fi

./tools/fileops.sh run -- db_diff --old $proc_old --new $proc_new --out $diff_proc_out
if [[ $? -ne 0 ]]; then
    exit 1  # Eroare aparuta in proc_snapshot
fi

# Verificam existenta fisierului
if [[ ! -e "$diff_proc_out" ]]; then
    exit 2
fi

# Verificam continutul
grep -q -E "^(Creat|Modificat|Sters): PID = [0-9]*$" $diff_proc_out
if [[ $? -ne 0 ]]; then
    exit 2
fi

exit 0



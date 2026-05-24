# 1. Creem un arbore de test in ./temp

test_dir="./tmp/t5_test_dir"

rm -rf "$test_dir"
mkdir "$test_dir"

for d in $(seq 1 6); do
    subdir="$test_dir/dir_$d"
    mkdir "$subdir"

    for f in $(seq 1 16); do
        touch "$subdir/file_$f"
    done

    for sd in a b c; do
        mkdir "$subdir/sub_$sd"
        for f in $(seq 1 10); do
            touch "$subdir/sub_$sd/file_$f"
        done
    done
done

# 2. Pornim managerul cu --simulate-work-ms si --pid-file
pid_file="./tmp/manager_pid.txt"
db_file="./data/t5_test.db"
work_ms=1000

rm $pid_file
./tools/fileops.sh run -- fileops_manager --workers 4 --root "$test_dir" --db "$db_file" --pid-file "$pid_file" --simulate-work-ms "$work_ms" &

# 3. Asteptam prin polling aparitia fisierului pid_file
max_timeout_ms=2000   # 2 secunde in ms
time_sleep_ms=200     # 0.2 secunde in ms
time_elapsed=0

while [[ ! -s "$pid_file" ]] && [[ "$time_elapsed" -le "$max_timeout_ms" ]]; do
    sleep 0.2
    let time_elapsed+=time_sleep_ms
done

if [[ "$time_elapsed" -gt "$max_timeout_ms" ]]; then
    echo "Timeout: Nu s-a putut gasi fisierul prin polling"
    exit 1
fi

# 4. Transmitem SIGUSR1 managerului dupa 3 secunde

manager_pid=$(cat "$pid_file")
sleep 3
kill -SIGUSR1 $manager_pid

# 5. Transmitem SIGTERM inaintea finalizarii
kill -SIGTERM $manager_pid

# 6. Verificam daca au ramas workeri zombie
ps aux | grep -q "fileops_worker" | tr -s ' ' | cut -d ' ' -f 8 |  grep -q "Z"
if [[ $? -eq 0 ]]; then
    echo "Au ramas workeri zombie"
    exit 1
fi

# 7. Verificam DB-ul
if [[ ! -e $db_file ]]; then
    echo "Nu exista DB!"
    exit 1
fi

./tools/fileops.sh run -- fileops_manager --db "$db_file" --verify
if [[ $? -ne 0 ]]; then
    echo "Verificarea a esuat!"
    exit 1
fi

field="complete"
out_dump=$(./tools/fileops.sh run -- fileops_manager --db "$db_file" --dump)
out_complete=$(echo $out_dump | cut -f3 -d' ')
complete_value=$(echo $out_complete | cut -f2 -d=)

if [[ $complete_value -ne 1 ]]; then
    echo "DB-ul nu a fost finalizat partial!"
    exit 1
fi

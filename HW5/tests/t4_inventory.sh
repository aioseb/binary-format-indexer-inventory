# Testul pentru T4 se considera finalizat cu succes daca:
# 1. Exista DB-ul dupa ce fileops_manager a fost rulat cu 8 workeri
#
# 2. fileops_manager --verify returneaza 0
#
# 3. Se verifica existenta anumitor campuri sumarizate de --dump

ipc_path="./data/test_ipc.mmap"
db_path="./data/test_inventory.db"
worker_count=8

./tools/fileops.sh run -- fileops_manager --root . --ipc "$ipc_path" --db "$db_path" --workers "$worker_count"
ret_val=$?

# Verifica daca fileops_manager a fost rulat cu succes
if [[ $ret_val -ne 0 ]]; then
    exit 1
fi

# Verifica existenta DB-ului
if [[ ! -e $db_path ]]; then
    exit 1
fi

# Verifica daca --verify returneaza 0
./tools/fileops.sh run -- fileops_manager --db "$db_path" --verify
ret_val=$?

if [[ $ret_val -ne 0 ]]; then
    exit 1
fi

# Verifica existenta anumitor campuri sumarizate de --dump
fields=( "magic" "complete" "file_record_count" "worker_count" )
out_dump=$(./tools/fileops.sh run -- fileops_manager --db "$db_path" --dump)
out_fields=$(echo "$out_dump" | cut -f1 -d=)

for field in "${fields[@]}"; do
    found=0

    for out_field in $out_fields; do
        if [[ $field == $out_field ]]; then
            found=1
        fi
    done

    if [[ $found -ne 1 ]];then
        exit 1  # Nu s-a regasit un camp in dump
    fi
done

exit 0  # Succes


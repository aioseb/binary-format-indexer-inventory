#!/bin/bash

# Creeaza structura proiectului temporar
temp_dir="./tmp/"
test_scene="${temp_dir}scenariu_test_src/"

mkdir -p "${test_scene}"
mkdir -p "${test_scene}app"
mkdir -p "${test_scene}lib"
mkdir -p "${test_scene}lib/include"

main_src="${test_scene}app/main_demo.c"
util_src="${test_scene}lib/util.c"
util_header="${test_scene}lib/include/util.h"

[[ -f $main_src ]] && rm $main_src
[[ -f $util_src ]] && rm $util_src
[[ -f $util_header ]] && rm $util_header

touch $main_src
touch $util_src
touch $util_header

# Scrie in fisierele sursa codul respectiv
echo "int util_add(int a, int b);" > $util_header
echo -e "#include \"include/util.h\"\nint util_add(int a, int b) { return a + b; }" > $util_src
echo -e "#include \"../lib/include/util.h\"\n#include <stdio.h>\nint main() {\n	printf(\"%i\", util_add(2, 3)); \nreturn 0;\n}" > $main_src

# Construim mini-proiectul
demo_dest="./tmp/demo_out.txt"
CFLAGS="-Itmp/scenariu_test_src/lib/include -std=c11 -Wall -Wextra"
./tools/fileops.sh build --src ./tmp/scenariu_test_src/

if [[ $? -ne 0 ]]; then
	exit_code=1
	echo "Output: NULL (cod eroare script $exit_code)" > $demo_dest
	exit $exit_code	# Eroare la compilare
fi

# Verificam daca exista executabilul si il rulam in caz afirmativ
if [[ ! -f ./bin/demo || ! -x ./bin/demo ]]; then
	exit_code=2
        echo "Output: NULL (cod eroare script $exit_code)" > $demo_dest
        exit $exit_code
fi

echo "Output: " > $demo_dest
./bin/demo >> ./tmp/demo_out.txt

exit_code=$?
if [[ $? -ne 0 ]]; then
        echo "Cod de exit al executabilului: $exit_code" >> $demo_dest
        exit $exit_code
fi

exit_code=0
echo -e "\nCod de exit al executabilului: $exit_code" >> $demo_dest

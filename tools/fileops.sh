#!/bin/bash

# Initializeaza directoarele si verifica daca exista compilatorul gcc
function init(){
	mkdir -p bin src include data logs reports tmp tests doc tools

	if [[ $(gcc -v 2> /dev/null ; echo $?) -ne 0 ]]; then
		echo "Compilatorul 'gcc' nu este instalat!" 1>&2
		return 3
	fi

	return 0
}

# Compileaza fisierele sursa; pune obiectele in /tmp/obj; pune executabilul lui main_*.c in /bin
function build(){
	src_dir="./src/" # Directorul default

	# Citeste argument cu argument
	while [[ $# -gt 0 ]]; do
		if [[ $1 == "--src" ]]; then
			src_dir=$2 # Citeste directorul primit ca argument
			shift ;
		fi
		shift
	done

	# Valideaza directorul cu fisiere sursa .c
	if [[ ! -d $src_dir || ! -r $src_dir ]]; then
		echo "$src_dir nu exista, nu este director, sau nu poate fi citit!"
		return 2
	fi

	# Plaseaza fisierele .obj in ./tmp/obj, iar executabilele in ./bin/
	dest_dir_obj="./tmp/obj/"
	dest_dir_elf="./bin/"

	declare -a obj_list	# Caile catre fisierele *.o ( fara main_ )
	declare -a elf_list	# Caile catre fisierele main_*.o
	obj_list=()
	elf_list=()

	mkdir -p $dest_dir_obj
	explicit_build $src_dir
	exit_code=$?

	if [[ $exit_code -ne 0 ]]; then
		return $exit_code
	fi

	# Compileaza fisierele main_*.c in executabile
	for main_file in "${elf_list[@]}"; do
		elf_name=${main_file%.o}
		elf_name=${elf_name#*/main_}

		elf_file_dest="${dest_dir_elf}${elf_name}"
		gcc $main_file "${obj_list[@]}" -o $elf_file_dest -Wall -Wextra

		if [[ $? -ne 0 ]]; then
			return 3	# Eroare la compilare
		fi
	done

	return 0
}

# Ruleaza executabilul dat ca parametru cu argumentele date
function run(){
	script_name=$1
	shift

	bin_dir="./bin/"

	while [[ $# -gt 0 ]]; do
		if [[ $1 == "--" ]]; then
			elf_path="${bin_dir}${2}"

			if [[ ! -f $elf_path || ! -x $elf_path ]]; then
				echo "Executabilul nu exista sau nu poate fi rulat!"
				return 2
			fi

			shift 2
			$elf_path $@	# Ruleaza executabilul cu argumentele date
			return $?
		fi
		shift
	done

	echo "Folosire: $script_name run -- <exe> [args...]"
	return 1
}

# Sterge artefacte (obiecte si executabile)
function clean(){
	path_dir_obj="./tmp/obj/"
	path_dir_elf="./bin/"

	rm "${path_dir_obj}"*	# Sterge toate obiectele
	rm "${path_dir_elf}"*	# Sterge toate executabilele

	return 0
}

# Ruleaza fiecare test script in parte si afiseaza rezultate
function test(){
	path_dir_tests="./tests/"

	# Initializeaza rapoartele
	path_reports_tests="./reports/T2_tests.txt"

	if [[ -f $path_reports_tests ]]; then
		rm $path_reports_tests
	fi
	touch $path_reports_tests
	all_valid=true

	for test_file in $(find "$path_dir_tests"); do
		if [[ $test_file == *.sh ]]; then
			$test_file
			exit_code=$?

			if [[ $exit_code -ne 0 ]]; then
				all_valid=false
				status="FAIL"
			else
				status="PASS"
			fi

			echo "${status}: $test_file" >> $path_reports_tests
		fi
	done

	if [[ $all_valid == true ]]; then
		return 0
	fi
	return 4
}

# Functii ajutatoare

# Compileaza fisierele *.c prin recursie explicita
function explicit_build(){
	sub_dir=$1

	for path in $sub_dir/* $sub_dir/.*; do
		if [[ -d $path ]]; then
			explicit_build $path || return $?
			path="${path%/*}";

		elif [[ $path == *.c ]]; then
			src_name=$(basename $path)
			obj_file_dest="${dest_dir_obj}${src_name%.c}".o

			# Efectueaza compilarea incrementala
			if [[ ! -f $obj_file_dest || $path -nt $obj_file_dest ]]; then
				gcc -c $path -o $obj_file_dest -Wall -Wextra

				if [[ $? -ne 0 ]]; then
					return 3 # Eroare la compilare
				fi
			fi

			if [[ $src_name == main_* ]]; then
				elf_list+=($obj_file_dest)
			else
				obj_list+=($obj_file_dest)
			fi
		fi
	done

	return 0
}

# Ne asiguram ca subcomenzile nu pot afecta timpii de start (mai ales in cazul subcomenzii 'test')
if [[ -z $start_timestamp && -z $start_milliseconds ]]; then
	start_timestamp=$(date "+%H:%M:%S_%3N")
	start_milliseconds=$(date "+%s%3N")
fi

subcmd=$1
return_val=0

if [[ $# -eq 0 ]]; then
	echo "Folosire: $0 init|build|run|clean|test"
	return_val=1
else
	shift # Eliminam subcomanda din parametri ca sa pastram doar argumentele

	case $subcmd in
		"init") init ; return_val=$? ;;
		"build") build $@ ; return_val=$? ;;
		"run") run $0 $@ ; return_val=$? ;;
		"clean") clean ; return_val=$? ;;
		"test") test ; return_val=$? ;;

		*) echo "Comanda nu exista!" ; return_val=2 ;;
	esac
fi

# Afiseaza in logs informatiile necesare
end_timestamp=$(date "+%H:%M:%S_%3N")
end_milliseconds=$(date "+%s%3N")

milliseconds_passed=$(( end_milliseconds - start_milliseconds ))

# Ne asiguram ca avem doar un singur log file (mai ales pentru cazul subcomenzii 'test')
if [[ -z $current_date && -z $log_path ]]; then
	current_date=$(date "+%Y%m%d_%H%M%S")
	log_path="./logs/fileops_${current_date}.log"
fi

touch $log_path
echo "Subcomanda rulata : ${subcmd}" >> $log_path
echo "Timestamp start : ${start_timestamp}" >> $log_path
echo "Timestamp end   : ${end_timestamp}" >> $log_path
echo "Timpul de executie (in milisecunde) : ${milliseconds_passed}" >> $log_path
echo "Exit code $0 : $return_val" >> $log_path

exit $return_val

#!/bin/bash

./tools/fileops.sh init

# Problema la faza de initiere
if [[ $? -ne 0 ]]; then
	exit 1
fi

dirs=( "./bin/" "./src/" "./include/" "./data/" "./logs/" "./reports/" "./tmp/" "./tests/" "./doc/" "./tools/" )
dirs_count="${#dirs[@]}"

# Verifica daca structura proiectului este buna
for (( dir_index=0; dir_index < dirs_count; dir_index++ )) ; do
	dir="${dirs[$dir_index]}"

	# Arunca o eroare daca una din directoare lipseste
	if [[ ! -d $dir ]]; then
		exit 2
	fi
done

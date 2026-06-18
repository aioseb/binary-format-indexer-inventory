# Custom Binary Format for DB's

## Documentation
The respective documentation for the fileops indexer, regular file inventory, signals and pipes can be found in the `doc` directory.

- fileops_indexer - `./doc/FORMAT.md`
- inventory - `./doc/T4_DB_FORMAT.md`
- IPC file for inventory - `./doc/MMAP_PROTOCOL.md`
- signals and pipes - `./doc/T5_CONTROL_PLANE.md`

## Commands
`./tools/fileops.sh` is a bash script that comes with a set of CLI commands.

- `./tools/fileops.sh init` - initiates the project structure
- `./tools/fileops.sh clean` - removes artifacts (objects from `./tmp/obj/` and executables from `./bin/`)
- `./tools/fileops.sh test` - runs every script from the `./tests` directory and produces a `./reports/T2_test.txt`; a test script `PASS`es if its return code is 0, or `FAIL`s otherwise.
- `./tools/fileops.sh build` - compiles C source files from `./src/` and outputs binaries of source files starting with `main_*`, and object files in `./tmp/obj/` for every .c file.
- `./tools/fileops.sh run -- <binary-file> [args]` - runs binary file from `./bin/` with the given args.

## TODO
- Translate error messages, comments and documentations from `./doc/` from Romanian to English.
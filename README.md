# Tftp client and server
Simple tftp client and server written in C++ with boost library.

## Requirements
  * boost library
  * gnu make

## Usage
  * Compile project using `make` command
  * Run `server` which will appear in `server` folder
    * By default server runs on `localhost:69`
  * Run `client` which will appear in `client` folder
    * run with `-r [fileName]` for reading or `-w [fileName]` for writing
  * You can also run binaries with `--help` to see all configurable options
  * Both client and server are logging to stdErr so just redirect it if you don't want to see this

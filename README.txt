WELCOME TO THE OFFICIAL REPOSITORY FOR THE COMMODORE 64 ULTIMATE
================================================================

Before you continue, be aware that this git repository uses some submodules. Make sure you also
clone them by issuing the following commands after cloning this repository:
$ git submodule init
$ git submodule update

* In order to build the various versions, you need the following toolchains:

C64U:
- RiscV GNU C/C++ Compiler (for RV32I), same as for U2/RiscV.
- Espressif IDF 5.x

These toolchains can be found in the docker image that can be recreated from
'docker/Dockerfile':

cd docker
docker build -t riscv .

The docker is also available on ghcr.io/gideonz/riscv:latest


Building targets
================
The targets can be built by just typing 'make' in the root of the repository, if you have
the toolchains installed and added to the path. If you haven't, you can use the docker.

docker run --rm -it -v "$(pwd)":/__w ghcr.io/gideonz/riscv:latest make u64ii

==> When using the top level makefile, the results are copied into the root of the project.

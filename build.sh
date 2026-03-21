#!/usr/bin/env bash
git submodule init
git submodule update
docker run --rm -v "$(pwd)":/__w ghcr.io/gideonz/riscv:latest make u64ii

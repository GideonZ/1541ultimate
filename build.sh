#!/usr/bin/env bash

docker run --rm -v "$(pwd)":/__w ghcr.io/gideonz/riscv:latest make

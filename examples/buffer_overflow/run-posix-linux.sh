#!/bin/bash

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../startup/third_party/bass24-linux/x64 \
	./buffer_overflow.elf "$@"


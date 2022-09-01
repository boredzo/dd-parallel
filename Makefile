CC=clang
prefix_name="prefix_$(shell uname).h"
CFLAGS=-include dd-parallel-posix/${prefix_name}
bin/dd-parallel: bin dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o
	clang dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o -o $@
bin:
	mkdir $@

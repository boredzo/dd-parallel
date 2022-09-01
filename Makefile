CC=clang
os_name="$(shell uname)"
prefix_name="prefix_${os_name}.h"
CFLAGS=-include dd-parallel-posix/${prefix_name}
LDFLAGS=-pthread -lm 
ifeq ($(os_name),Linux)
LDFLAGS+=-lbsd
endif

bin/dd-parallel: bin dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o
	clang $(LDFLAGS) dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o -o $@
bin:
	mkdir $@

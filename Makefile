CC=clang
LD=clang
os_name=$(shell uname)
prefix_name="prefix_${os_name}.h"
CFLAGS=-g -include dd-parallel-posix/${prefix_name}
LDFLAGS=-pthread -lm 
ifeq ($(os_name),Linux)
LDFLAGS+=-lbsd
endif

bin/dd-parallel: bin dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o
	$(LD) dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o $(LDFLAGS) -o $@
bin:
	mkdir $@

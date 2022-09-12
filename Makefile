include Makefile.defs
CC=$(compiler)
LD=$(linker)
os_name=$(shell uname)
prefix_name="prefix_${os_name}.h"
CFLAGS=$(cflags)
LDFLAGS=$(ldflags)

all: bin/dd-parallel
clean:
	rm */*.o
	rm bin/*
.PHONY: all clean

bin/dd-parallel: bin dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o
	$(LD) dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o $(LDFLAGS) -o $@
bin:
	mkdir $@

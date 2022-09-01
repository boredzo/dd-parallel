CC=clang

bin/dd-parallel: bin dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o
	clang dd-parallel-posix/main.o dd-parallel-posix/formatting_utils.o -o $@
bin:
	mkdir $@

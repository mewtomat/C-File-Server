.phony: all clean run

all: client-shell.c get-one-file-sig.c
	gcc client-shell.c -o shell
	gcc get-one-file-sig.c -o get-one-file-sig 

run:
	./shell

clean:
	rm -f get-one-file-sig shell
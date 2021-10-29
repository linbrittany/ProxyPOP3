all: pop3filter/args.c pop3filter/main.c utils/*.c
	gcc --std=c11 -pedantic -pedantic-errors -Wall -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200112L pop3filter/args.c pop3filter/main.c utils/*.c -g -o main

clean:
	rm -f main
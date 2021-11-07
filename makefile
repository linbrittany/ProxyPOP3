all: pop3filter/*.c utils/*.c
	gcc --std=c11 -pedantic  -pthread -pedantic-errors -Wall -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200112L pop3filter/*.c utils/*.c -g -o main

clean:
	rm -f main
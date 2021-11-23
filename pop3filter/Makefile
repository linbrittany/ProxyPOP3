CC       = gcc
SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET := pop3filter
rm       = rm -rf
CFLAGS   = -c -g --std=c99 -pedantic -pedantic-errors -Wall -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200809L -I./include -I./../utils/include -I./../admin/include
LINKER 	 = gcc
LFLAGS 	 = -g --std=c99 -pedantic -pedantic-errors -Wall -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200809L  -lpthread -pthread

all: comp link

%.o : %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	@$(rm) $(OBJECTS)
	@$(rm) main

link: $(OBJECTS)
	$(LINKER) $(LFLAGS) $(OBJECTS) ./../utils/*.o ./../admin/*.o -o main

comp:$(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES)

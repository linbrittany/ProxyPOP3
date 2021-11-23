# LINKER 	 = gcc
# LFLAGS 	 = -g --std=c99 -pedantic -pedantic-errors -Wall -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200809L  -lpthread -pthread
# rm       = rm -rf

all: proxy

proxy:
	@cd utils; make all
	@cd admin; make all
	@cd pop3filter; make all
	@cd client; make all

clean:
	@cd utils; make clean
	@cd pop3filter; make clean
	@cd admin; make clean
	@cd client; make clean

# pop3server: proxy utils admin
# 	@$(LINKER) $(LFLAGS) ./utils/*.o ./pop3filter/*.o ./admin/*.o -o main

# admincli: utils
# 	@$(LINKER) $(LFLAGS) ./utils/*.o -o client

.PHONY: all clean

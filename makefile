include Makefile.inc

rm       = rm -rf

all: proxy link

proxy:
	@cd pop3filter; make all
	@cd utils; make all

clean:
	@cd utils; make clean
	@cd pop3filter; make clean
	@$(rm) main

link:
	@$(LINKER) $(LFLAGS) ./utils/*.o ./pop3filter/*.o -o main

.PHONY: all clean
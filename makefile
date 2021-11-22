include Makefile.inc

rm       = rm -rf

all: proxy link

proxy:
	@cd pop3filter; make all
	@cd utils; make all
	@cd admin; make all
clean:
	@cd utils; make clean
	@cd pop3filter; make clean
	@cd admin; make clean
	@$(rm) main

link:
	@$(LINKER) $(LFLAGS) ./utils/*.o ./pop3filter/*.o ./admin/*.o -o main

.PHONY: all clean
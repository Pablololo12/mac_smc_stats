
CC     = gcc
CFLAGS = -O2 -Wall
INC    = -framework IOKit
PREFIX = /usr/local
EXEC   = cpu_temp

build : $(EXEC)

clean : 
	rm $(EXEC)

$(EXEC) : main.c
	$(CC) $(CFLAGS) $(INC) -o $@ $?

install : $(EXEC)
	@install -v $(EXEC) $(PREFIX)/bin/$(EXEC)

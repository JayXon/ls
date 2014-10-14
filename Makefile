CFLAGS	+= -std=c99 -D_POSIX_C_SOURCE=200809L -D_BSD_SOURCE -Wall

ls: 	ls.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f ls

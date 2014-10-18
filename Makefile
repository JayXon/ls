CFLAGS	+= -std=c99 -D_POSIX_C_SOURCE=200809L -D_BSD_SOURCE -Wall -O3
LDFLAGS	+= -lm -lbsd

.PHONY:	clean

ls: 	ls.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm ls

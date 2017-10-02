CC = gcc
CFLAGS = -Wall -g

mysort: mysort.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

.PHONY: clean
clean:
	rm -f mysort.o mysort

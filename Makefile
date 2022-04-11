CC = gcc
CFLAGS = -O3 -Wall -pthread
OTHERFLAGS = -lcrypto -lssl

all: webproxy

webproxy: webproxy.c
	$(CC) $(CFLAGS) webproxy.c -o webproxy $(OTHERFLAGS)

clean:
	rm -f webproxy; 
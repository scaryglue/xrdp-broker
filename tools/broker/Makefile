# Makefile for broker tools
CC = gcc
CFLAGS = -Wall -O2 -I/usr/include -I/usr/local/include
LDFLAGS = -lnng -ljansson

TARGETS = broker agent

all: $(TARGETS)

broker: broker.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

agent: agent.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

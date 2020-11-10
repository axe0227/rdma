.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

TARGETS    := client server

all: ${TARGETS}

client: utils.o client.o
	${LD} -o $@ $^ ${LDLIBS}

server: utils.o server.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${TARGETS}
CC = g++
LIBS =
CFLAGS = -std=c++11
UNAME := $(shell uname)
THREAD_LIB = thread
PIPE_LIB = pipe
SERVER_OUTPUT = np_server
CLIENT_OUTPUT = np_client

ifeq ($(UNAME), SunOS)
LIBS = -lsocket -lnsl
endif

ifeq ($(UNAME), Linux)
CFLAGS += -pthread -lrt
endif


CFLAGS += -lpthread

# etc

all: pipe tinythread server client

pipe: pipe.c
	gcc -c -std=c99 -o ${PIPE_LIB} pipe.c ${LIBS}

tinythread: tinythread.cpp
	${CC} -c ${CFLAGS} -o ${THREAD_LIB} tinythread.cpp ${LIBS}

server: server.cpp
	${CC} ${CFLAGS} -o ${SERVER_OUTPUT} server.cpp ${PIPE_LIB} ${THREAD_LIB} ${LIBS}

client: client.cpp
	${CC} ${CFLAGS} -o ${CLIENT_OUTPUT} client.cpp ${THREAD_LIB} ${LIBS}

clean:
	rm ${THREAD_LIB} ${SERVER_OUTPUT} ${CLIENT_OUTPUT}
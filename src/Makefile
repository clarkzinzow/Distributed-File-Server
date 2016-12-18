#
# To compile, type "make" or make "all"
# To remove files, type "make clean"
#
OBJS = server.o udp.o libmfs.so mfs.o client.o
TARGET = server

CC = gcc
CFLAGS = -g -Wall
current_dir := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
.SUFFIXES: .c .o 

all: server client libmfs.so

server: server.c udp.o
	$(CC) $(CFLAGS) -fPIC server.c -o server udp.o

udp.o: udp.c udp.h
	$(CC) $(CFLAGS) -fPIC -c udp.c
	
mfs.o: mfs.c mfs.h udp.h
	$(CC) $(CFLAGS) -fPIC -c mfs.c 
	
libmfs.so: mfs.o udp.o
	$(CC) -shared -o libmfs.so mfs.o udp.o

client: client.c libmfs.so
	$(CC) -L$(current_dir) $(CFLAGS) client.c -o client -lmfs

clean:
	-rm -f $(OBJS) server client *~

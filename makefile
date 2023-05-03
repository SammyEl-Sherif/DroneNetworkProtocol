# Makefile for client and server

CC = gcc
OBJCS = drone.c

CFLAGS =  -g -Wall
# setup for system
nLIBS =

all: drone 

drone8: $(OBJCS)
	$(CC) $(CFLAGS) -o $@ $(OBJCS) $(LIBS) -lm

clean:
	rm drone

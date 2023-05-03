# Makefile for client and server

CC = gcc
OBJCS = drone8.c

CFLAGS =  -g -Wall
# setup for system
nLIBS =

all: drone8 

drone8: $(OBJCS)
	$(CC) $(CFLAGS) -o $@ $(OBJCS) $(LIBS) -lm

clean:
	rm drone8 

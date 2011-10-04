CC=gcc
CC_OPT=
CC_INC=
CC_LIB=-levent

all: allow 

allow: allow.o
	$(CC) -o allow allow.o $(CC_LIB)

allow.o: allow.c
	$(CC) $(CC_OPT) $(CC_INC) -c -o allow.o allow.c

clean:
	rm -f allow allow.o

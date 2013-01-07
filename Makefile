CFLAGS = -g -Wall
LIBS = 

all: abuntix-setup

abuntix-setup: abuntix-setup.o
	$(CC) $(CFLAGS) $(LIBS) -o abuntix-setup abuntix-setup.o

clean:
	rm -f *.o abuntix-setup

CFLAGS = -g -Wall

all: config abuntix-setup

config: config.o
	$(CC) $(CFLAGS) -o config config.o

abuntix-setup: abuntix-setup.o
	$(CC) $(CFLAGS) -o abuntix-setup abuntix-setup.o

clean:
	rm -f *.o config abuntix-setup

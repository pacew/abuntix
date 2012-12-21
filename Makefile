CFLAGS = -g -Wall

abuntix-setup: abuntix-setup.o
	$(CC) $(CFLAGS) -o abuntix-setup abuntix-setup.o

CFLAGS = -g -Wall

abuntix-setup: abuntix-setup.o
	$(CC) $(CFLAGS) -o abuntix-setup abuntix-setup.o

bakim: bakim.o
	$(CC) $(CFLAGS) -o bakim bakim.o

clean:
	rm -f *.o abuntix-setup bakim

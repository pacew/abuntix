CFLAGS = -g -Wall

all: abuntix-setup bakim

abuntix-setup: abuntix-setup.o
	$(CC) $(CFLAGS) -o abuntix-setup abuntix-setup.o

bakim: bakim.o
	$(CC) $(CFLAGS) -o bakim bakim.o

caps:
	setcap cap_linux_immutable,cap_dac_override,cap_chown+ep bakim

clean:
	rm -f *.o abuntix-setup bakim

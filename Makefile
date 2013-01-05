CFLAGS = -g -Wall
LIBS = 

all: abuntix-setup bakim

abuntix-setup: abuntix-setup.o
	$(CC) $(CFLAGS) $(LIBS) -o abuntix-setup abuntix-setup.o

bakim: bakim.o
	$(CC) $(CFLAGS) $(LIBS) -o bakim bakim.o

caps:
	setcap cap_linux_immutable,cap_dac_override,cap_chown,cap_fowner+ep bakim

clean:
	rm -f *.o abuntix-setup bakim

clear:
	-chattr -Rf -i /big 2>/dev/null
	rm -rf /big/*

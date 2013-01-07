bin_PROGRAMS = bakim
bakim_SOURCES = bakim.c

install-exec-hook:
	setcap cap_linux_immutable,cap_dac_override,cap_chown,cap_fowner+ep /usr/local/bin/bakim
	mkdir -p /big

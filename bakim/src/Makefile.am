bin_PROGRAMS = bakim
bakim_SOURCES = bakim.c

install-exec-hook:
	sudo setcap cap_linux_immutable,cap_dac_override,cap_chown,cap_fowner+ep /usr/local/bin/bakim
	sudo mkdir -p /big

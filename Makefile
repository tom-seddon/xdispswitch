.PHONY:xdispswitch
xdispswitch :
	gcc -o xdispswitch xdispswitch.c -Wall -lX11 -lXinerama -std=c99 -D_BSD_SOURCE -D_XOPEN_SOURCE 


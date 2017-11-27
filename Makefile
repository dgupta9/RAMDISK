ramdisk:	ramdisk.c
	gcc -Wall -Wno-unused-but-set-variable -Wno-unused-value  -Wno-unused-variable ramdisk.c `pkg-config fuse --cflags --libs` -o ramdisk

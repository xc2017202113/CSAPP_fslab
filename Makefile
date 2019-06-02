#
# Students' Makefile for the Filesystem Lab
#
STUID = 2017000000-2017000001
#Leave no spaces between or after your STUID!!!
#If you finished this lab on your own, fill like this:2017000000-None
VERSION = 1
HANDINDIR = /home/handin-fs

MNTDIR = mnt
VDISK = vdisk

CC = gcc
CFLAGS = -Wall

OBJS = disk.o fs.c

debug: umount clean fuse
	./fuse -f $(MNTDIR)

mount: umount clean fuse
	./fuse $(MNTDIR)

umount:
	-fusermount -u $(MNTDIR)

fuse: $(OBJS)
	echo $(abspath $(lastword $(MAKEFILE_LIST))) > fuse~
    ifeq ($(VDISK), $(wildcard $(VDISK)))
		rm -rf $(VDISK)
    endif
	mkdir $(VDISK)
    ifeq ($(MNTDIR), $(wildcard $(MNTDIR)))
		rm -rf $(MNTDIR)
    endif
	mkdir $(MNTDIR)
	$(CC) $(CFLAGS) -o fuse $(OBJS) -DFUSE_USE_VERSION=29 -D_FILE_OFFSET_BITS=64 -lfuse

disk.o: disk.c disk.h

handin:
	chmod 600 fs.c
	cp fs.c $(HANDINDIR)/$(STUID)-$(VERSION)-fs.c
	chmod 400 $(HANDINDIR)/$(STUID)-$(VERSION)-fs.c

clean:
	-rm -f *~ *.o fuse
	-rm -rf $(VDISK) $(MNTDIR)

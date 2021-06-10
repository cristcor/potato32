montaje = punto_montaje
fichero = image.img

fuse_flags = -D_FILE_OFFSET_BITS=64 -lfuse -pthread

potato32 : potato32.o 
	gcc -g -o $@  $^ ${fuse_flags}
	mkdir -p $(montaje)
	
potato32.o : potato32.c potato32.h
	gcc -g -c -o $@  $< ${fuse_flags}


mount: potato32
	./potato32 $(fichero) $(montaje)

debug: potato32
	./potato32 -d $(fichero) $(montaje)

empty: potato32
	./potato32 -d $(montaje)

empty_debug: potato32
	./potato32 -d $(montaje)

umount:
	fusermount -u $(montaje)
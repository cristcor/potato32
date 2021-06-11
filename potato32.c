#define FUSE_USE_VERSION 26
#include <stdlib.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include "potato32.h"

//////////////////////////////////////
//			Aux functions			//
//////////////////////////////////////
static uint8_t *read_tubercular_data(struct potato32 *data, uint32_t dir){
	int res = 0;
	//Check buffers
	for(unsigned i = 0; i<BUFFER_ENTRIES; i++){
		//If is NULL stop check
		if(data->buffer[i]==NULL) break;
		//If is inside that buffer
		else if(data->buffer[i]->data_dir <= dir && data->buffer[i]->data_dir + (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES) > dir ){
			//Return the buffer data + (diference in clusters between desired dir and buffer init)*size of cluster
			data->lastUsed = i;
			return data->buffer[i]->data + (dir - data->buffer[i]->data_dir)*BYTES_PER_CLUSTER;
		}
	}

	//Check if that position is generated
	fseek(data->file, 0, SEEK_END); // seek to end of file
	long int size = ftell(data->file); // get current file pointer

	long int desired = sizeof(struct tubercular_file_system_information)+
		(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4)+
		dir*BYTES_PER_CLUSTER);

	//If it is not generated and a few more clusters, generate them
	if(desired + (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES)*BYTES_PER_CLUSTER > size){
		res = fseek(data->file, desired + (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES)*BYTES_PER_CLUSTER, SEEK_SET);
	}
	//Error ocurred with fseek
	if(res){
		return NULL;
	}

	//Fill the next buffer with them
	fseek(data->file, desired, SEEK_SET);

	//Check next buffer to be filled
	data->lastUsed+1 == BUFFER_ENTRIES ? data->lastUsed = 0 : data->lastUsed++ ;

	//Initialize that buffer if it has been never used
	if(data->buffer[data->lastUsed] == NULL){
		data->buffer[data->lastUsed] = (struct tubercular_buffer*) malloc(sizeof(struct tubercular_buffer));
	} else {
		free(data->buffer[data->lastUsed]->data);
	}

	//Fill the buffer with the data
	data->buffer[data->lastUsed]->data_dir = dir;
	fread(data->buffer[data->lastUsed]->data, sizeof(uint8_t), MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES, data->file);

	//Return data
	return data->buffer[data->lastUsed]->data;
}

//////////////////////////////////////
//			Core functions			//
//////////////////////////////////////
static void *p32_init(struct fuse_conn_info *conn){
	struct potato32 *data = (struct potato32 *) fuse_get_context()->private_data;
	return data;
}
static void p32_destroy(void *private_data){

}

//////////////////////////////////////
//			Dir functions			//
//////////////////////////////////////
static int p32_mkdir(const char *dir, mode_t mode){
	return 0;
}
static int p32_rmdir(const char *dir){
	return 0;
}
static int p32_readdir(const char *dir, void *buff, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
	return 0;
}

//////////////////////////////////////
//			File functions			//
//////////////////////////////////////
static int p32_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi){
	return 0;
}
static int p32_open(const char *path, struct fuse_file_info *fi){
	return 0;
}
static int p32_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi){
	return 0;
}
static int p32_unlink(const char *path){
	return 0;
}
static int p32_create(const char *path, mode_t mode, struct fuse_file_info *fi){
	return 0;
}
static int p32_rename(const char *from, const char *to){
	return 0;
}

//////////////////////////////////////
//			Attr functions			//
//////////////////////////////////////
static int p32_getattr(const char *path, struct stat *stbuff){
	return 0;
}


//////////////////////////////////////
//			Struct functions			//
//////////////////////////////////////
static struct fuse_operations fuse_ops = {
	.init 		=	p32_init,
	.destroy	=	p32_destroy,

	.mkdir		=	p32_mkdir,
	.rmdir		=	p32_rmdir,
	.readdir	=	p32_readdir,

	.read 		=	p32_read,
	.open 		=	p32_open,
	.write 		=	p32_write,
	.unlink 	=	p32_unlink,
	.create 	=	p32_create,
	.rename 	=	p32_rename,

	.getattr 	=	p32_getattr,
	//.setattr 	=	p32_setattr,
	//.chmod 	=	p32_chmod,
};


int main(int argc, char *argv[])
{
    struct potato32 *data;
    data=malloc(sizeof(struct potato32) + (sizeof(struct tubercular_use_entry) * ((pow(2, ADDRESS_BITS))/4)-1) );

    struct tubercular_container_head *root = (struct tubercular_container_head*) malloc(sizeof(struct tubercular_container_head));

    //No param or last param is an option
    if ((argc < 1) || (argv[argc-1][0] == '-'))
    {
        printf("Param error\n");
        exit(-1);
    }

    //Check if an image is given. Otherwise create a new one
    unsigned mustcreate = 0;

    if(argc<2) mustcreate = 1;
    else if(argv[argc-2][0]=='-' && argv[argc-1][0] != '-') mustcreate = 1;

    if(mustcreate){//No image to load
        printf("- Creating File System image...\n");
        //Size of each part and sum of all of them
        int tutbytes = (pow(2, ADDRESS_BITS))/4;
        int tdrbytes = MINIMUM_TDR_SIZE*BYTES_PER_CLUSTER;
        int tfsibytes = sizeof(struct tubercular_file_system_information);

        int size = tutbytes+tdrbytes+tfsibytes;
        
        //Create a file with that size, fill it and close
        printf("- Allocating image...\n");
        FILE *fp = fopen("blankimage", "w");
        fseek(fp, size , SEEK_SET);

        //Fill TFSI
        printf("- Filling TFSI...\n");
        struct tubercular_file_system_information *tfsi = 
        (struct tubercular_file_system_information *) malloc(sizeof(struct tubercular_file_system_information));

        tfsi->free_tubercular_regions = (pow(2, ADDRESS_BITS))-1;
        tfsi->first_free_tubercular_region = 1;
        tfsi->number_of_potatoes = 0;
        tfsi->number_of_tubercular_containers = 1;

        fwrite(tfsi, sizeof(struct tubercular_file_system_information), 1, fp);

        //Fill TUT
        printf("- Filling TUT...\n");
        struct tubercular_use_entry *tut = 
        (struct tubercular_use_entry *) malloc(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4));
        for(long i = 0; i<(pow(2, ADDRESS_BITS))/4; i++){
            if(i==0) tut[i].info = 0b00111111;
            else tut[i].info = 0b00000000;
        }

        fwrite(tut, sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4), 1, fp);

        //Fill TDR
        printf("- Initializing the Tubercular Data Region root...\n");
        root = (struct tubercular_container_head*) malloc(sizeof(struct tubercular_container_head));

        root->pathname[0] = '/';
        root->pathname[1] = '\0';

        printf("- Filling root pointers...\n");
        for(long i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS; i++){
        	if(i==0) root->files[i] = 0x00000001;
        	else if (i==1) root->files[i] = 0x00000002;
        	else root->files[i] = 0x00000000;
        }

        root->next = 0x00000000;

        fwrite(root, sizeof(struct tubercular_container_head), 1, fp);

        //Test empty dir
        printf("- Creating test Tubercular Container...\n");
        struct tubercular_container_head *testdir = (struct tubercular_container_head*) malloc(sizeof(struct tubercular_container_head));

        testdir->pathname[0] = 't';
        testdir->pathname[1] = 'e';
        testdir->pathname[2] = 's';
        testdir->pathname[3] = 't';
        testdir->pathname[4] = '\0';

        printf("- Filling Tubercular Container pointers...\n");
        for(long i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS; i++){
        	testdir->files[i] = 0x00000000;
        }

        testdir->next = 0x00000000;

        fwrite(testdir, sizeof(struct tubercular_container_head), 1, fp);

        //Test empty file
        printf("- Creating test Potatoe...\n");
        struct potatoe_head *testfile = (struct potatoe_head*) malloc(sizeof(struct potatoe_head));

        testfile->filename[0] = 'e';
        testfile->filename[1] = 'm';
        testfile->filename[2] = 'p';
        testfile->filename[3] = 't';
        testfile->filename[4] = 'y';
        testfile->filename[5] = '\0';
        testfile->extension[0] = 't';
        testfile->extension[1] = 'x';
        testfile->extension[2] = 't';
        testfile->filesize = 0;
        testfile->create_time = time(0);
        testfile->modify_time = time(0);
        testfile->acces_time = time(0);
        testfile->next = 0x00000000;

        fwrite(testfile, sizeof(struct potatoe_head), 1, fp);

        printf("- Closing...\n");

        fclose(fp);

        //Fill data
        data->imagepath = "blankimage";
        data->tfsi = tfsi;
        data->tut = tut;
        data->st_uid = getuid();
        data->st_gid = getgid();

    } else {//Image ready to be loaded
        //Fill data with data from image
        data->imagepath = strdup(argv[argc-2]);

        printf("- Reading file: %s\n", data->imagepath);
        FILE *fp = fopen(data->imagepath, "r");

        //Reading TFSI
        printf("-- Reading Tubercular File System Information\n");
        data->tfsi = (struct tubercular_file_system_information*) malloc(sizeof(struct tubercular_file_system_information));
        unsigned tam = fread(data->tfsi, sizeof(struct tubercular_file_system_information*), 1, fp);
        if(tam!= sizeof(struct tubercular_file_system_information)){
            printf("--- Error reading Tubercular File System Information\n");
            if(feof(fp)) printf("--- End Of File\n");
            else if(ferror(fp)) printf("--- Error\n");
            else printf("--- Unknow error\n");
            printf("--- %d of %lu bytes\n\n", tam, sizeof(struct tubercular_file_system_information));
            return -1;
        }

        //Reading TUT
        printf("-- Reading Tubercular Use Table\n");
        data->tut = (struct tubercular_use_entry*) malloc(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4));
        tam = fread(data->tut, sizeof(struct tubercular_use_entry), (pow(2, ADDRESS_BITS))/4, fp);
        if(tam!=sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4)){
            printf("--- Error reading first entry of Tubercular Use Table\n");
            if(feof(fp)) printf("--- End Of File\n");
            else if(ferror(fp)) printf("--- Error\n");
            else printf("--- Unknow error\n");
            printf("--- %d of %lu bytes\n\n", tam, sizeof(struct tubercular_use_entry));
            return -1;
        }

        //Read Root
        printf("-- Reading Root Tubercular Container\n");
        tam = fread(root, sizeof(struct tubercular_container_head), 1, fp);
        if(tam!=sizeof(struct tubercular_container_head)){
            printf("--- Error reading first Tubercular Data Region (Root\n");
            if(feof(fp)) printf("--- End Of File\n");
            else if(ferror(fp)) printf("--- Error\n");
            else printf("--- Unknow error\n");
            printf("--- %d of %lu bytes\n\n", tam, sizeof(struct tubercular_container_head));
            return -1;
        }
        printf("\n");

        //Closing file
        fclose(fp);

        //Remove image path from args
        argv[argc-2] = argv[argc-1];
        argv[argc-1] = NULL;
        argc--;
    }

    //Set buffers to NULL
    for(unsigned i = 0; i<BUFFER_ENTRIES; i++){
        data->buffer[i] = NULL;
    }

    //Show information about created/readed data
    printf("\n- Tubercular File System Potato32 status:\n");
    printf("-- Free Tubercular Regions: %d\n", data->tfsi->free_tubercular_regions);
    printf("--- First free Tubercular Region: %d\n", data->tfsi->first_free_tubercular_region);
    printf("-- Number of Potatoes: %d\n", data->tfsi->number_of_potatoes);
    printf("-- Number of Tubercular Containers: %d\n\n", data->tfsi->number_of_tubercular_containers);

    //Check if the first Tubercular Data Region is the root
    printf("- Checking root integrity...\n");

    if(root->pathname[0]!='/' || root->pathname[1]!='\0') {
        printf("-- Incorrect pathname, error\n\n");
        return -1;
    }
    if(data->tut[0].info & 0b00000011 != 0b00000011){
        printf("-- Incorrect TUT entry, error\n\n");
        return -1;
    }

    printf("-- Integrity checking passed\n\n");

    printf("- Starting FUSE\n");

    data->file=fopen(data->imagepath, "rw");

    //Init fuse
    return fuse_main(argc, argv, &fuse_ops, data);
}
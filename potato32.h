#ifndef _POTATO32_H_
#define _POTATO32_H_
#endif

#include <stdint.h>
#include <time.h>
#include <stdio.h>

////////////////////////////////////
//		DEFINITIONS OF SIZES	  //
////////////////////////////////////
//Cluster size = 1kB
#define BYTES_PER_CLUSTER 	1024
#define NAME_CHAR_LENGTH	128
#define MAX_CLUSTERS_ON_MEMORY 128*1024	//128MB
#define BUFFER_ENTRIES		4

//COULD BE ANY NUMBER. 24 BITS UP TO 16GB. 32 BITS UP TO 4TB
#define ADDRESS_BITS		24

#define MINIMUM_TDR_SIZE  	256

//Number of bytes of data per potatoe head
#define POTATOE_HEAD_DATA_BYTES		BYTES_PER_CLUSTER-NAME_CHAR_LENGTH-3-2*sizeof(uint32_t)-3*sizeof(time_t)
//Number of bytes of data per potatoe
#define POTATOE_DATA_BYTES			BYTES_PER_CLUSTER-sizeof(uint32_t)

//Number of pointers per tubercular container head
#define TUBERCULAR_CONTAINER_HEAD_PTRS 	(BYTES_PER_CLUSTER-NAME_CHAR_LENGTH)/sizeof(uint32_t)-1
//Number of pointers per tubercular container
#define TUBERCULAR_CONTAINER_PTRS 		(BYTES_PER_CLUSTER/sizeof(uint32_t))-1


////////////////////////////////////////
//		TUBERCULAR FS INFORMATION	  //
////////////////////////////////////////

//Tubercular File System Information(TFSI)
struct tubercular_file_system_information {
	uint32_t free_tubercular_regions;
	uint32_t first_free_tubercular_region;
	uint32_t number_of_potatoes;
	uint32_t number_of_tubercular_containers;
}  __attribute__ ((__packed__)); // no Padding

////////////////////////////////////////
//	  	  TUBERCULAR USE TABLE	  	  //
////////////////////////////////////////

//Tubercular Use Entry (TUE)
struct tubercular_use_entry {
	uint8_t info;
}  __attribute__ ((__packed__)); // no Padding


////////////////////////////////////////
//		  TUBERCULAR DATA REGION	  //
////////////////////////////////////////
struct potatoe_head{
	//Head
	char filename[NAME_CHAR_LENGTH];
	char extension[3];
	uint32_t filesize;	//In bytes
	time_t create_time;
	time_t modify_time;
	time_t acces_time;
	//Data
	uint8_t data[POTATOE_HEAD_DATA_BYTES];
	//Next ptr
	uint32_t next;
}  __attribute__ ((__packed__)); // no Padding

struct potatoe{
	uint8_t data[POTATOE_DATA_BYTES];
	uint32_t next;
}  __attribute__ ((__packed__)); // no Padding

struct tubercular_container_head{
	char pathname[NAME_CHAR_LENGTH];
	uint32_t files[TUBERCULAR_CONTAINER_HEAD_PTRS];
	uint32_t next;
}  __attribute__ ((__packed__)); // no Padding

struct tubercular_container{
	uint32_t files[TUBERCULAR_CONTAINER_PTRS];
	uint32_t next;
}  __attribute__ ((__packed__)); // no Padding

////////////////////////////////////////
//		  BUFFER TUBERCULAR STRUCT	  //
////////////////////////////////////////
struct tubercular_buffer{
	uint32_t data_dir;
	uint8_t data[MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES];
};

////////////////////////////////////////
//		  GENERAL TUBERCULAR STRUCT	  //
////////////////////////////////////////
struct potato32{
	char *imagepath;
	struct tubercular_file_system_information *tfsi;
	struct tubercular_use_entry *tut;
	FILE *file;
	uid_t st_uid;		//User ID
    gid_t st_gid;		//Group ID
    time_t time;

    //Buffer things
    struct tubercular_buffer *buffer[BUFFER_ENTRIES];
    uint lastUsed;
};
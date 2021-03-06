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
	fprintf(stderr, "---- Looking for TDR position %u\n", dir);
	int res = 0;
	//Check buffers
	for(unsigned i = 0; i<BUFFER_ENTRIES; i++){
		//If is NULL stop check
		if(data->buffer[i]==NULL) break;
		fprintf(stderr, "---- Checking buffer %d with address %u\n", i, data->buffer[i]->data_dir);
		//If is inside that buffer
		if(data->buffer[i]->data_dir <= dir && data->buffer[i]->data_dir + (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES) > dir ){
			fprintf(stderr, "---- Returning\n");
			//Return the buffer data + (diference in clusters between desired dir and buffer init)*size of cluster
			data->lastUsed = i;
			return data->buffer[i]->data + (dir - data->buffer[i]->data_dir)*BYTES_PER_CLUSTER;
		}
	}

	//Check if that position is generated
	fseek(data->file, 0, SEEK_END); // seek to end of file
	long int size = ftell(data->file); // get current file pointer

	long int tdrstart = sizeof(struct tubercular_file_system_information)+
		(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4));

	long int desired = tdrstart + dir*BYTES_PER_CLUSTER;

	//If it is not generated and a few more clusters, generate them
	if(desired + (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES)*BYTES_PER_CLUSTER > size){
		res = fseek(data->file, desired + (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES)*BYTES_PER_CLUSTER, SEEK_SET);
	}
	//Error ocurred with fseek
	if(res){
		return NULL;
	}

	//Check next buffer to be filled
	if(!(data->buffer[data->lastUsed] == NULL)) data->lastUsed = (data->lastUsed==BUFFER_ENTRIES-1 ? data->lastUsed = 0 : data->lastUsed++);

	//Initialize that buffer if it has been never used
	if(data->buffer[data->lastUsed] == NULL){
		fprintf(stderr, "---- Creating empty buffer %d with address %u\n", data->lastUsed, dir);
		data->buffer[data->lastUsed] = (struct tubercular_buffer*) malloc(sizeof(struct tubercular_buffer));
	} else {
		//Save on disc
		fprintf(stderr, "---- Saving buffer %d with address %u\n", data->lastUsed, dir);
		fseek(data->file, tdrstart + (data->buffer[data->lastUsed]->data_dir)*BYTES_PER_CLUSTER, SEEK_SET);
		fwrite(&data->buffer[data->lastUsed]->data, sizeof(uint8_t), MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES, data->file);
	}

	//Move to the read position
	fseek(data->file, desired, SEEK_SET);

	//Fill the buffer with the data
	fprintf(stderr, "---- Filling buffer...\n");

	data->buffer[data->lastUsed]->data_dir = dir;
	fread(data->buffer[data->lastUsed]->data, sizeof(uint8_t), MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES, data->file);

	//Return data
	return &(data->buffer[data->lastUsed]->data);
}

static unsigned its_container(struct potato32 *data, uint32_t dir){
	uint32_t entry = dir/4;
	unsigned offset = dir%4;

	struct tubercular_use_entry tue = data->tut[entry];
	return ((tue.info >> ((offset*2)+1))  & 0x01);
}

static unsigned its_empty(struct potato32 *data, uint32_t dir){
	uint32_t entry = dir/4;
	unsigned offset = dir%4;

	struct tubercular_use_entry tue = data->tut[entry];
	return !((tue.info >> ((offset*2)))  & 0x01);
}

static void set_tue(struct potato32 *data, uint32_t dir, unsigned use, unsigned fold){
	uint32_t entry = dir/4;
	unsigned offset = dir%4;

	struct tubercular_use_entry* tue = &(data->tut[entry]);

	if(use){
		tue->info |= 1UL << (offset*2);
	} else {
		tue->info &= ~(1UL << (offset*2));
	}

	if(fold){
		tue->info |= 1UL << (offset*2+1);
	} else {
		tue->info &= ~(1UL << (offset*2+1));
	}
}

static unsigned isASCII(char *dir, unsigned pos){
	if( !((dir[pos]>='a' && dir[pos]<='z') || (dir[pos]>='A' && dir[pos]<='Z')) ){
		return 0;
	}
	return 1;
}

static unsigned get_dir_of_container(struct potato32 *data, const char *odir, uint32_t* result){
	char *dir = (char*) malloc(strlen(odir));
	strcpy(dir, odir);
	fprintf(stderr, "- Looking for container %s with length %ld\n", dir, strlen(dir));

	unsigned auxindex = strlen(odir)-1;
	while(!isASCII(dir, auxindex) && auxindex>0){
		dir[auxindex]='\0';
		auxindex--;
	}
	
	if(strcmp(dir, "/")==0 || (dir[0]=='/' && dir[1] == '\0')){
		fprintf(stderr, "-- Looking for root direction\n");
		*result = 0x00000000;
		return 0;
	}


	char delim[] = "/";
	char *s = strtok((char*)dir, delim);

	uint32_t folder = 0x00000000;
	struct tubercular_container_head* head = NULL;
	struct tubercular_container* body = NULL;
	struct tubercular_container_head* next = NULL;

	unsigned pos = 0;
	unsigned found;

	while(s!=NULL){
		//Load folder head
		//fprintf(stderr,"- Looking for %s\n", s);
		head = (struct tubercular_container_head*) read_tubercular_data(data, folder);
		found = 0;
		pos = 0;
		//Look inside of the head
		while(pos<TUBERCULAR_CONTAINER_HEAD_PTRS){
			//Check if not the end
			if(head->files[pos]==0x00000000){
				return 1;
			}
			//Check if its container
			if(!its_container(data, head->files[pos])){
				pos++;
				continue;
			}
			//Load next
			next = (struct tubercular_container_head*) 
				read_tubercular_data(data, head->files[pos]);
			//Check if its the objective
			if(!strcmp(s, next->pathname)){
				found = 1;
				folder = head->files[pos];
				break;
			}
			pos++;
		}
		//If not found in head, look through extensions of head
		if(!found){
			//If theres not extensions, return NULL
			if(head->next == 0x00000000){
				return 1;
			}
			//Else look while theres extensions
			body = (struct tubercular_container*) read_tubercular_data(data, head->next);
			while(body!=NULL){
				pos = 0;
				while(pos<TUBERCULAR_CONTAINER_PTRS){
					//Check if not the end
					if(body->files[pos]==0x00000000){
						return 1;
					}
					//Check if its container
					if(!its_container(data, body->files[pos])){
						pos++;
						continue;
					}
					next = (struct tubercular_container_head*) 
						read_tubercular_data(data, body->files[pos]);
					//Check if its the objective
					if(!strcmp(s, next->pathname)){
						found = 1;
						folder = body->files[pos];
						break;
					}
					pos++;
				}
				if(found) break;
				//Set next body
				if(body->next==0x00000000){
					body = NULL;
				} else {
					body = (struct tubercular_container*) read_tubercular_data(data, body->next);
				}
			}
		}

		//If still not found, return NULL
		if(!found){
			return 1;
		}
		s = strtok(NULL, delim);
	}
	*result = folder;
	return 0;
}

static unsigned look_inside_for(struct potato32 *data, const char *lfname, const uint32_t folder, uint32_t* result){
	struct tubercular_container_head* prev = (struct tubercular_container_head*) read_tubercular_data(data, folder);

	//Look in head
	fprintf(stderr, "- Looking inside head\n");
	unsigned found = 0;
	uint32_t dir = 0;
	for(int i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS && !found; i++){
		//If its empty return enoent
		if(prev->files[i]==0x00000000) return 1;

		//Check name
		if(its_container(data, prev->files[i])){	//If container
			struct tubercular_container_head* aux = (struct tubercular_container_head*) read_tubercular_data(data, prev->files[i]);

    		fprintf(stderr, "-- Checking folder %s\n", aux->pathname);
			//If its
			if(strcmp(lfname, aux->pathname)==0){
				dir = prev->files[i];
				found = 1;
			}
		} else {			//If file
			struct potatoe_head* aux = (struct potatoe_head*) read_tubercular_data(data, prev->files[i]);

			char* name;

			if(strlen(aux->extension)!=0){
				name = (char*) malloc((strlen(aux->filename)+4));

    			strcpy(name, aux->filename);
    			strcpy(&name[strlen(aux->filename)], ".");
    			strcpy(&name[strlen(aux->filename)+1], aux->extension);
			} else {
				name = (char*) malloc((strlen(aux->filename)));

    			strcpy(name, aux->filename);
			}

			//If its
			fprintf(stderr, "-- Comparing %s & %s \n", lfname, name);
			if(strcmp(lfname, name) == 0){
				fprintf(stderr, "-- Are equals\n");
				dir = prev->files[i];
				found = 1;
				break;
			}
		}
	}

	//If not found look in extensions
	if(!found && prev->next != 0x00000000){
		fprintf(stderr, "- Looking inside bodies\n");
		struct tubercular_container* cont = (struct tubercular_container*) read_tubercular_data(data, prev->next);
		while(cont!=NULL && !found){
			//Iterate
			for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && !found; i++){
				//If its empty return enoent
				if(cont->files[i]==0x00000000) return -ENOENT;

				//Check name
				if(its_container(data, cont->files[i])){	//If container
					struct tubercular_container_head* aux = (struct tubercular_container_head*) read_tubercular_data(data, cont->files[i]);
					//If its
					if(strcmp(lfname, aux->pathname)==0){
						dir = cont->files[i];
						found = 1;
					}
				} else {			//If file
					struct potatoe_head* aux = (struct potatoe_head*) read_tubercular_data(data, cont->files[i]);

					char* name;

					if(strlen(aux->extension)!=0){
						name = (char*) malloc((strlen(aux->filename)+4));

		    			strcpy(name, aux->filename);
		    			strcpy(&name[strlen(aux->filename)], ".");
		    			strcpy(&name[strlen(aux->filename)+1], aux->extension);
					} else {
						name = (char*) malloc((strlen(aux->filename)));

		    			strcpy(name, aux->filename);
					}

					//If its
					if(!strcmp(lfname, name)){
						dir = cont->files[i];
						found = 1;
					}
				}
			}
			if(cont->next!=0x00000000){
				cont = (struct tubercular_container*) read_tubercular_data(data, cont->next);
			} else {
				cont = NULL;
			}
		}
	}
	if(!found) return 1;

	*result = dir;

	return 0;
}

static unsigned remove_val_from_dir(struct potato32 *data, uint32_t val, uint32_t dir){
	//Get dir
	struct tubercular_container_head* tch = (struct tubercular_container_head*) read_tubercular_data(data, dir);
	struct tubercular_container* bodyrm = NULL;
	struct tubercular_container* bodyrp = NULL;

	//Look for the index to remove on the head
	int rmindex = -1;
	for(int i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS && rmindex == -1; i++){
		if(tch->files[i]==val) rmindex = i;
	}

	//Must be in the body
	if(rmindex == -1){
		if(tch->next == 0x00000000) return 1;
		bodyrm = (struct tubercular_container*) read_tubercular_data(data, tch->next);
		while(rmindex == -1){
			for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && rmindex == -1; i++){
				if(bodyrm->files[i]==val) rmindex = i;
			}
			if(rmindex==-1){
				if(bodyrm->next == 0x00000000){
					return 1;
				} else {
					bodyrm = (struct tubercular_container*) read_tubercular_data(data, bodyrm->next);
				}
			}
		}
	}

	//RM dir already found
	//LF RP dir
	int rpindex = -2;

	//Check on head if found on head
	if(bodyrm==NULL){
		for(int i = rmindex+1; i<TUBERCULAR_CONTAINER_HEAD_PTRS && rpindex == -2; i++){
			if(tch->files[i]==0x00000000) rpindex = i-1;
		}
	}

	//If still not found, start looking on body
	if(rpindex==-2){
		if(bodyrm == NULL){
			bodyrp = (struct tubercular_container*) read_tubercular_data(data, tch->next);
		} else {
			bodyrp = (struct tubercular_container*) bodyrm;
		}

		while(rpindex == -2){
			for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && rpindex == -2; i++){
				if(bodyrp->files[i]==val) rpindex = i;
			}
			if(rpindex==-2){
				if(bodyrp->next == 0x00000000){
					rpindex = TUBERCULAR_CONTAINER_PTRS-1;
				} else {
					bodyrp = (struct tubercular_container*) read_tubercular_data(data, bodyrp->next);
				}
			}
		}
	}

	uint32_t newval = 0;
	//Replace from body
	if(bodyrp == NULL){
		newval = tch->files[rpindex];
		tch->files[rpindex] = 0;
	} else {
		newval = bodyrp->files[rpindex];
		bodyrp->files[rpindex] = 0;
	}

	if(bodyrm == NULL){
		if(tch->files[rmindex] == newval) tch->files[rmindex] = 0x00000000;
		else tch->files[rmindex] = newval;
	} else {
		if(bodyrm->files[rmindex] == newval) bodyrm->files[rmindex] = 0x00000000;
		else bodyrm->files[rmindex] = newval;
	}

	return 0;
}

//////////////////////////////////////
//			Core functions			//
//////////////////////////////////////
static void *p32_init(struct fuse_conn_info *conn){
	struct potato32 *data = (struct potato32 *) fuse_get_context()->private_data;

	return data;
}
static void p32_destroy(void *private_data){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	//Reset ptr
	fseek(data->file, 0, SEEK_SET);

	//Save TFSI
	fprintf(stderr, "Saving TFSI\n");
	fwrite(&data->tfsi, sizeof(struct tubercular_file_system_information), 1, data->file);

	//Save TUT
	fprintf(stderr, "Saving TUT\n");
	unsigned tuttam = 0;
	tuttam = fwrite(&data->tut, sizeof(struct tubercular_use_entry), pow(2, ADDRESS_BITS)/4, data->file);
	if(tuttam != pow(2, ADDRESS_BITS)/4){
		fprintf(stderr, "Only %u units got written!\n", tuttam);
	}

	//Save all the buffers
	fprintf(stderr, "Saving buffers\n");
	long int tdrstart = sizeof(struct tubercular_file_system_information)+
		(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4));

	for(unsigned i = 0; i<BUFFER_ENTRIES; i++){
		if(data->buffer[i] == NULL) break;
		fprintf(stderr, "Writing buffer %d with TDR address %u and local address %lu\n", i, data->buffer[i]->data_dir, 
			tdrstart + (data->buffer[i]->data_dir)*BYTES_PER_CLUSTER);
		fseek(data->file, tdrstart + (data->buffer[i]->data_dir)*BYTES_PER_CLUSTER, SEEK_SET);
		unsigned tam = 0;
		tam = fwrite(&(data->buffer[i]->data), sizeof(uint8_t), (MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES), data->file);
		if(tam!=MAX_CLUSTERS_ON_MEMORY/BUFFER_ENTRIES) fprintf(stderr, "Only %u bytes got written!\n", tam);
	}

	fclose(data->file);


	fprintf(stderr, "Bye!\n");
}

//////////////////////////////////////
//			Dir functions			//
//////////////////////////////////////
static int p32_mkdir(const char *path, mode_t mode){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	//Look for an empty region
	uint32_t used_dir = data->tfsi->first_free_tubercular_region;
	struct tubercular_container_head* cr = (struct tubercular_container_head*) read_tubercular_data(data, data->tfsi->first_free_tubercular_region);
	data->tfsi->free_tubercular_regions--;
	data->tfsi->number_of_tubercular_containers++;

	//Set region tue
	set_tue(data, used_dir, 1, 1);

	//Look for next empty
	while(!its_empty(data, data->tfsi->first_free_tubercular_region)) data->tfsi->first_free_tubercular_region++;

	//Get path of previous folder
	int index = strlen(path)-1;
	while(index>=0 && path[index]!='/') index--;
	if(index == -1) return -ENOENT;
	char* fold = (char*) malloc((index+1)*sizeof(char));
	strncpy(fold, path, index+1);

	//Fill new container
	strcpy(cr->pathname, &(path[index+1]));


	//Get that folder
	uint32_t dir = 0;
	if(get_dir_of_container(data, fold, &dir)) return -ENOENT;
	struct tubercular_container_head* prev = (struct tubercular_container_head*) read_tubercular_data(data, dir);

	//Look in head
	unsigned found = 0;
	dir = 0;
	for(int i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS && !found; i++){
		//If its empty return enoent
		if(prev->files[i]==0x00000000){
			found = 1;
			prev->files[i] = used_dir;
			return 0;
		}
	}

	//If not found look in extensions
	if(!found && prev->next != 0x00000000){
		struct tubercular_container* cont = (struct tubercular_container*) read_tubercular_data(data, prev->next);
		while(cont!=NULL && !found){
			//Iterate
			for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && !found; i++){
				//If its empty return enoent
				if(cont->files[i]==0x00000000){
					found = 1;
					cont->files[i]= used_dir;
					return 0;
				}
			}
			if(cont->next!=0x00000000){
				cont = (struct tubercular_container*) read_tubercular_data(data, cont->next);
			} else {
				//New empty region
				struct tubercular_container* ncont = (struct tubercular_container*) read_tubercular_data(data, data->tfsi->first_free_tubercular_region);

				//Set region tue
				set_tue(data, data->tfsi->first_free_tubercular_region, 1, 1);

				//Add as next
				cont->next = data->tfsi->first_free_tubercular_region;

				//Look for next empty
				while(!its_empty(data, data->tfsi->first_free_tubercular_region)) data->tfsi->first_free_tubercular_region++;

				//Format
				ncont->files[0] = used_dir;
				for(uint32_t i = 0; i<TUBERCULAR_CONTAINER_PTRS; i++){
					ncont->files[0] = 0x00000000;
				}

				ncont->next = 0x00000000;

				return 0;
			}
		}
	}

	return 1;
}

static int p32_rmdir(const char *dir){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	//Get containing folder
	int index = strlen(dir)-1;
	while(index>=0 && dir[index]!='/') index--;
	if(index<0) return -ENOENT;
	char* fold = (char*)malloc(sizeof(char)*(index+1));
	strncpy(fold, dir, index+1);
	fprintf(stderr, "Removing from %s\n", fold);

	uint32_t container =0;
	if(get_dir_of_container(data, fold, &container)) return -ENOENT;

	uint32_t dcontainer = 0;
	if(look_inside_for(data, &(dir[index+1]), container, &dcontainer)) return -ENOENT;

	if(remove_val_from_dir(data, dcontainer, container)) return -ENOENT;

	return 0;
}
static int p32_readdir(const char *dir, void *buff, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	fprintf(stderr,"- Reading dir %s\n", dir);

	uint32_t mem = 0;
	if(get_dir_of_container(data, dir, &mem)) return -ENOENT;
	fprintf(stderr,"-- Found\n");

	struct tubercular_container_head* head = (struct tubercular_container_head*) read_tubercular_data(data, mem);
	unsigned end = 0;

	//Add head pointers
	fprintf(stderr,"- Looking inside of %s\n", (char*)&(head->pathname));
	for(unsigned pos = 0; pos<TUBERCULAR_CONTAINER_HEAD_PTRS && !end; pos++){
		fprintf(stderr,"-- Mem position 0x%08x\n", head->files[pos]);
		if(head->files[pos] == 0){
			fprintf(stderr,"- End of container\n");
			end = 1;
		} else {
			fprintf(stderr,"-- Entry found\n");
			if(its_container(data, head->files[pos])){
				struct tubercular_container_head* aux = (struct tubercular_container_head*) read_tubercular_data(data, head->files[pos]);
				fprintf(stderr,"--- Adding container %s\n", (char*)&(aux->pathname));
				if(filler(buff, aux->pathname, NULL, 0)!=0) return -ENOMEM;
			} else {
				struct potatoe_head* aux = (struct potatoe_head*) read_tubercular_data(data, head->files[pos]);

				char* name;
				fprintf(stderr, "-- Adding file %s.%s\n", aux->filename, aux->extension);
				if(strlen(aux->extension)!=0){
					name = (char*) malloc((strlen(aux->filename)+4));

    				strcpy(name, aux->filename);
    				strcpy(&name[strlen(aux->filename)], ".");
    				strcpy(&name[strlen(aux->filename)+1], aux->extension);
				} else {
					name = (char*) malloc((strlen(aux->filename)));

    				strcpy(name, aux->filename);
				}

				fprintf(stderr,"--- Adding file %s\n", name);
				if(filler(buff, name, NULL, 0)!=0) return -ENOMEM;
				free(name);
			}
		}
	}

	//If theres extensions of the folder
	if(head->next != 0x00000000){
		struct tubercular_container* body = (struct tubercular_container*) read_tubercular_data(data, head->next);
		while(body!=NULL){
			for(unsigned pos = 0; pos<TUBERCULAR_CONTAINER_HEAD_PTRS && !end; pos++){
				if(head->files[pos]==0x00000000){
					end = 1;
				} else {
					if(its_container(data, head->files[pos])){
						struct tubercular_container_head* aux = (struct tubercular_container_head*) read_tubercular_data(data, head->files[pos]);
						if(filler(buff, aux->pathname, NULL, 0)!=0) return -ENOMEM;
					} else {
						struct potatoe_head* aux = (struct potatoe_head*) read_tubercular_data(data, head->files[pos]);

						char* name = (char*) malloc((strlen(aux->filename)+4)*sizeof(char));

						strcpy(name, (char*)&(aux->filename));
						strcpy(&name[strlen(aux->filename)], ".");
						strcpy(&name[strlen(name)-4], (char*)&(aux->extension));

						if(filler(buff, name, NULL, 0)!=0) return -ENOMEM;
						free(name);
					}
				}
			}
			if(body->next == 0x00000000) body = NULL;
			else body = (struct tubercular_container*) read_tubercular_data(data, body->next);
		}
	}
	if(filler(buff, ".", NULL, 0)!=0) return -ENOMEM;
	if(filler(buff, "..", NULL, 0)!=0) return -ENOMEM;
	return 0;
}

//////////////////////////////////////
//			File functions			//
//////////////////////////////////////
static int p32_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi){
	return 0;
}
static int p32_open(const char *path, struct fuse_file_info *fi){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	int index = strlen(path)-1;
	while(path[index]!='/' && index>=0) index--;
	if(index<0) return -ENOENT;

	char* str = (char*) malloc(strlen(path)-index);
	strcpy(str, &(path[index+1]));

	char* fold = (char*) malloc(index+1);
	strncpy(str, path, index+1);

	uint32_t folddir = 0;
	uint32_t filedir = 0;
	if(get_dir_of_container(data, fold, &folddir)) return -ENOENT;
	if(look_inside_for(data, str, folddir, &filedir)) return -ENOENT;

	return 0;
}
static int p32_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi){
	return 0;
}
static int p32_unlink(const char *path){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	//Get containing folder
	int index = strlen(path)-1;
	while(index>=0 && path[index]!='/') index--;
	if(index<0) return -ENOENT;
	char* fold = (char*)malloc(sizeof(char)*(index+1));
	strncpy(fold, path, index+1);
	fprintf(stderr, "Removing from %s\n", fold);

	uint32_t container =0;
	if(get_dir_of_container(data, fold, &container)) return -ENOENT;

	uint32_t dcontainer = 0;
	if(look_inside_for(data, &(path[index+1]), container, &dcontainer)) return -ENOENT;

	if(remove_val_from_dir(data, dcontainer, container)) return -ENOENT;

	return 0;
}
static int p32_create(const char *path, mode_t mode, struct fuse_file_info *fi){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	//Look for an empty region
	uint32_t used_dir = data->tfsi->first_free_tubercular_region;
	struct potatoe_head* cr = (struct potatoe_head*) read_tubercular_data(data, data->tfsi->first_free_tubercular_region);
	data->tfsi->free_tubercular_regions--;
	data->tfsi->number_of_potatoes++;

	//Set region tue
	set_tue(data, used_dir, 1, 0);

	//Look for next empty
	while(!its_empty(data, data->tfsi->first_free_tubercular_region)) data->tfsi->first_free_tubercular_region++;

	//Get path of previous folder
	int index = strlen(path)-1;
	while(index>=0 && path[index]!='/') index--;
	if(index == -1) return -ENOENT;
	char* fold = (char*) malloc((index+1)*sizeof(char));
	strncpy(fold, path, index+1);

	//Fill new potato
	//Get extension
	int ext = strlen(path)-1;
	while(path[ext]!='.' && ext>=index){
		ext--;
	}
	if(ext<index){
		strcpy(cr->filename, &(path[index+1]));
	} else {
		strncpy(cr->extension, &(path[ext+1]), 3);

		strncpy(cr->filename, &(path[index+1]), ext-index-1);
	}

	cr->filesize = 0;	//In bytes
	cr->create_time = time(0);
	cr->modify_time = time(0);
	cr->acces_time = time(0);
	cr->next = 0x00000000;


	//Get that folder
	uint32_t dir = 0;
	if(get_dir_of_container(data, fold, &dir)) return -ENOENT;
	struct tubercular_container_head* prev = (struct tubercular_container_head*) read_tubercular_data(data, dir);

	//Look in head
	unsigned found = 0;
	dir = 0;
	for(int i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS && !found; i++){
		//If its empty return enoent
		if(prev->files[i]==0x00000000){
			found = 1;
			prev->files[i] = used_dir;
			return 0;
		}
	}

	//If not found look in extensions
	if(!found && prev->next != 0x00000000){
		struct tubercular_container* cont = (struct tubercular_container*) read_tubercular_data(data, prev->next);
		while(cont!=NULL && !found){
			//Iterate
			for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && !found; i++){
				//If its empty return enoent
				if(cont->files[i]==0x00000000){
					found = 1;
					cont->files[i]= used_dir;
					return 0;
				}
			}
			if(cont->next!=0x00000000){
				cont = (struct tubercular_container*) read_tubercular_data(data, cont->next);
			} else {
				//New empty region
				struct tubercular_container* ncont = (struct tubercular_container*) read_tubercular_data(data, data->tfsi->first_free_tubercular_region);

				//Set region tue
				set_tue(data, data->tfsi->first_free_tubercular_region, 1, 1);

				//Add as next
				cont->next = data->tfsi->first_free_tubercular_region;

				//Look for next empty
				while(!its_empty(data, data->tfsi->first_free_tubercular_region)) data->tfsi->first_free_tubercular_region++;

				//Format
				ncont->files[0] = used_dir;
				for(uint32_t i = 0; i<TUBERCULAR_CONTAINER_PTRS; i++){
					ncont->files[0] = 0x00000000;
				}

				ncont->next = 0x00000000;

				return 0;
			}
		}
	}

	return 1;
}
static int p32_rename(const char *from, const char *to){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;

	int indexf = strlen(from)-1;
	int indext = strlen(to)-1;

	while(indexf>=0 && from[indexf]!='/') indexf--;
	while(indext>=0 && to[indext]!='/') indext--;

	if(indexf<0 || indext<0) return -ENOENT;

	char* foldf = (char*)malloc(sizeof(char)*(indexf+1));
	char* foldt = (char*)malloc(sizeof(char)*(indext+1));
	strncpy(foldf, from, indexf+1);
	strncpy(foldt, to, indext+1);

	fprintf(stderr, "- Got folders\n");

	struct potatoe_head* ph;
	uint32_t file;
	//Check if we stay in the same folder
	if(strcmp(foldt, foldf)==0){	//Same folder
		fprintf(stderr, "- Same folder: %s && %s\n", foldf, foldt);
		//Get container dir
		uint32_t contdir = 0;
		if(get_dir_of_container(data, foldf, &contdir)) return -ENOENT;
		fprintf(stderr, "- Got container\n");
		//Get file dir
		file = 0;
		if(look_inside_for(data, &(from[indexf+1]), contdir, &file)) return -ENOENT;

		fprintf(stderr, "- Got file dir\n");

		//Get file
		ph = (struct potatoe_head*) read_tubercular_data(data, file);
		fprintf(stderr, "- Got file\n");
	} else {						//Different folder
		fprintf(stderr, "- Different folder\n");
		//Get old container dir
		uint32_t contdir = 0;
		if(get_dir_of_container(data, foldf, &contdir)) return -ENOENT;

		//Get file dir
		file = 0;
		if(look_inside_for(data, &(from[indexf+1]),contdir, &file)) return -ENOENT;
		ph = (struct potatoe_head*) read_tubercular_data(data, file);

		//Remove from old dir
		if(remove_val_from_dir(data, file, contdir)) return -ENOENT;

		//Get new container dir
		uint32_t ncontdir = 0;
		if(get_dir_of_container(data, foldt, &ncontdir)) return -ENOENT;

		//Put new dir
		struct tubercular_container_head* nhead = (struct tubercular_container_head*) read_tubercular_data(data, ncontdir);

		unsigned put = 0;

		//Insert in head
		for(int i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS && !put; i++){
			if(nhead->files[i]==0x00000000){
				put = 1;
				nhead->files[i] = file;
			}
		}

		//Insert in created body
		if(!put && nhead->next!=0x00000000){
			struct tubercular_container* nbody = (struct tubercular_container*) read_tubercular_data(data, nhead->next);
			while(!put && nbody!=NULL){
				for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && !put; i++){
					if(nbody->files[i]==0x00000000){
						put = 1;
						nbody->files[i] = file;
					}
				}
				if(!put){
					if(nbody->next == 0x00000000) nbody = NULL;
					else nbody = (struct tubercular_container*) read_tubercular_data(data, nbody->next);
				}
			}
		}

		//Could not insert in any created body
		if(!put){
			uint32_t* ptr;
			if(nhead->next == 0x00000000) ptr = (uint32_t*) &(nhead->next);
			else{
				struct tubercular_container* nbody = (struct tubercular_container*) read_tubercular_data(data, nhead->next);
				while(nbody->next!=0x00000000){
					nbody = (struct tubercular_container*) read_tubercular_data(data, nbody->next);
				}
				ptr = (uint32_t*) &(nbody->next);
			}

			//Get free direction and lf next one
			*ptr = data->tfsi->first_free_tubercular_region;
			data->tfsi->first_free_tubercular_region++;
			while(!its_empty(data, data->tfsi->first_free_tubercular_region)) data->tfsi->first_free_tubercular_region++;

			//Change TUT entry
			set_tue(data, *ptr, 1, 0);

			//Format new dir
			struct tubercular_container* cont = (struct tubercular_container*) read_tubercular_data(data, *ptr);
			cont->next = 0x00000000;
			cont->files[0] = file;
			for(int i = 1; i<TUBERCULAR_CONTAINER_PTRS; i++){
				cont->files[i] = 0x00000000;
			}
		}

	}

	if(its_container(data, file)){
		struct tubercular_container_head* tchr = (struct tubercular_container_head*) read_tubercular_data(data, file);

		strcpy(tchr->pathname, &(to[indext+1]));
	} else {
		//Get extensions and change name and extension
		int newext = strlen(to);
		while(newext>indext && to[newext]!='.') newext--;
		if(newext==indext){	//No extension
			fprintf(stderr, "- No extension\n");
			strcpy(ph->filename, &(to[indext+1]));
			ph->filename[strlen(to)-indext-1] = '\0';
			ph->extension[0] = '\0';
		} else {
			fprintf(stderr, "- With extension\n");
			strncpy(ph->filename, &(to[indext+1]), newext-indext-1);
			ph->filename[newext-indext-1] = '\0';
			strcpy(ph->extension, &(to[newext+1]));
		}
	}

	return 0;
}

//////////////////////////////////////
//			Attr functions			//
//////////////////////////////////////
static int p32_getattr(const char *path, struct stat *stbuf){
	struct potato32 *data = (struct potato32* ) fuse_get_context()->private_data;
	fprintf(stderr, "- Getting attributes of %s\n", path);
	//If its root
	if(strcmp(path, "/")==0){
		fprintf(stderr, "-- Its root\n");
		//Fill stat
		struct tubercular_container_head* head = (struct tubercular_container_head*) read_tubercular_data(data, 0x00000000);
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 0;


    	off_t size = 1;
    	uint32_t next = head->next;
    	struct tubercular_container* body;

    	while(next != 0x00000000){
    		size++;
    		body = (struct tubercular_container*) read_tubercular_data(data, next);
    		next = body-> next;
    	}

    	stbuf->st_size = size*BYTES_PER_CLUSTER;
    	stbuf->st_blocks = size;
    	stbuf->st_uid = data->st_uid;
    	stbuf->st_gid = data->st_gid;
    	stbuf->st_atime = data->time;
    	stbuf->st_mtime = data->time;
    	stbuf->st_ctime = data->time;

    	return 0;
	}

	//Get path of previous folder
	int index = strlen(path)-1;
	while(index>=0 && path[index]!='/') index--;
	if(index == -1) return -ENOENT;
	char* fold = (char*) malloc((index+1)*sizeof(char));
	strncpy(fold, path, index+1);
	fprintf(stderr, "- Containing folder is %s\n", fold);
	//Get that folder
	fprintf(stderr, "- Getting memory address\n");
	uint32_t dir = 0;
	if(get_dir_of_container(data, fold, &dir)) return -ENOENT;
	fprintf(stderr, "- Got %u\n", dir);
	struct tubercular_container_head* prev = (struct tubercular_container_head*) read_tubercular_data(data, dir);

	//Look in head
	fprintf(stderr, "- Looking inside head\n");
	unsigned found = 0;
	dir = 0;
	for(int i = 0; i<TUBERCULAR_CONTAINER_HEAD_PTRS && !found; i++){
		//If its empty return enoent
		if(prev->files[i]==0x00000000) return -ENOENT;

		//Check name
		if(its_container(data, prev->files[i])){	//If container
			struct tubercular_container_head* aux = (struct tubercular_container_head*) read_tubercular_data(data, prev->files[i]);

    		fprintf(stderr, "-- Checking folder %s\n", aux->pathname);
			//If its
			if(strcmp(&(path[index+1]), aux->pathname)==0){
				dir = prev->files[i];
				found = 1;
			}
		} else {			//If file
			struct potatoe_head* aux = (struct potatoe_head*) read_tubercular_data(data, prev->files[i]);

			char* name;
		
			if(strlen(aux->extension)!=0){
				name = (char*) malloc((strlen(aux->filename)+4));

    			strcpy(name, aux->filename);
    			strcpy(&name[strlen(aux->filename)], ".");
    			strcpy(&name[strlen(aux->filename)+1], aux->extension);
			} else {
				name = (char*) malloc((strlen(aux->filename)));

    			strcpy(name, aux->filename);
			}
			//If its
			fprintf(stderr, "-- Comparing %s & %s \n", &(path[index+1]), name);
			if(strcmp(&(path[index+1]), name) == 0){
				fprintf(stderr, "-- Are equals\n");
				dir = prev->files[i];
				found = 1;
				break;
			}
		}
	}

	//If not found look in extensions
	fprintf(stderr, "- Looking inside bodies\n");
	if(!found && prev->next != 0x00000000){
		struct tubercular_container* cont = (struct tubercular_container*) read_tubercular_data(data, prev->next);
		while(cont!=NULL && !found){
			//Iterate
			for(int i = 0; i<TUBERCULAR_CONTAINER_PTRS && !found; i++){
				//If its empty return enoent
				if(cont->files[i]==0x00000000) return -ENOENT;

				//Check name
				if(its_container(data, cont->files[i])){	//If container
					struct tubercular_container_head* aux = (struct tubercular_container_head*) read_tubercular_data(data, cont->files[i]);
					//If its
					if(strcmp(&(path[index+1]), aux->pathname)==0){
						dir = cont->files[i];
						found = 1;
					}
				} else {			//If file
					struct potatoe_head* aux = (struct potatoe_head*) read_tubercular_data(data, cont->files[i]);

					char* name = (char*) malloc((strlen(aux->filename)+4));

		    		strcat(name, aux->filename);
		    		strcat(name, ".");
		    		strcat(name, aux->extension);

					//If its
					if(!strcmp(&(path[index+1]), name)){
						dir = cont->files[i];
						found = 1;
					}
				}
			}
			if(cont->next!=0x00000000){
				cont = (struct tubercular_container*) read_tubercular_data(data, cont->next);
			} else {
				cont = NULL;
			}
		}
	}
	if(!found) return -ENOENT;

	fprintf(stderr, "- Retrieving data from that memory address\n");
	if(its_container(data, dir)){
		//Fill stat
		struct tubercular_container_head* head = (struct tubercular_container_head*) read_tubercular_data(data, dir);
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 0;


    	off_t size = 1;
    	uint32_t next = head->next;
    	struct tubercular_container* body;

    	while(next != 0x00000000){
    		size++;
    		body = (struct tubercular_container*) read_tubercular_data(data, next);
    		next = body-> next;
    	}

    	stbuf->st_size = size*BYTES_PER_CLUSTER;
    	stbuf->st_blocks = size;
    	stbuf->st_uid = data->st_uid;
    	stbuf->st_gid = data->st_gid;
    	stbuf->st_atime = data->time;
    	stbuf->st_mtime = data->time;
    	stbuf->st_ctime = data->time;

	} else {
		struct potatoe_head* phead = (struct potatoe_head*) read_tubercular_data(data, dir);
		stbuf->st_mode = S_IFREG | 0755;
    	stbuf->st_nlink = 0;
    	stbuf->st_size = phead->filesize;

    	blksize_t blk = 1;
    	uint32_t next = phead->next;
    	struct potatoe* body;

    	while(next != 0x00000000){
    		blk++;
    		body = (struct potatoe*) read_tubercular_data(data, next);
    		next = body-> next;
    	}

    	stbuf->st_blocks = blk;
    	stbuf->st_uid = data->st_uid;
    	stbuf->st_gid = data->st_gid;
    	stbuf->st_atime = phead->acces_time;
    	stbuf->st_mtime = phead->modify_time;
    	stbuf->st_ctime = phead->create_time;
	}

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

	//.read 		=	p32_read,
	.open 		=	p32_open,
	//.write 		=	p32_write,
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

    printf("All sizes must be %d\n", BYTES_PER_CLUSTER);
    printf("Sizes:\n");
    printf("\tTubercular Container Head: \t%lu\n", sizeof(struct tubercular_container_head));
    printf("\tTubercular Container: \t\t%lu\n", sizeof(struct tubercular_container));
    printf("\tPotatoe Head: \t\t\t%lu\n", sizeof(struct potatoe_head));
    printf("\tPotatoe: \t\t\t%lu\n\n", sizeof(struct potatoe_head));

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

    long unsigned tam = 0;

    if(mustcreate){//No image to load
        printf("- Creating File System image...\n");
        //Size of each part and sum of all of them
        int tutbytes = (pow(2, ADDRESS_BITS))/4;
        int tdrbytes = MINIMUM_TDR_SIZE*BYTES_PER_CLUSTER;
        int tfsibytes = sizeof(struct tubercular_file_system_information);

        int size = tutbytes+tdrbytes+tfsibytes;
        
        //Create a file with that size, fill it and close
        printf("- Allocating image...\n");
        FILE *fp = fopen("blankimage", "w+");
        fseek(fp, size , SEEK_SET);
        fseek(fp, 0, SEEK_SET);

        //Fill TFSI
        printf("- Filling TFSI...\n");
        struct tubercular_file_system_information *tfsi = 
        (struct tubercular_file_system_information *) malloc(sizeof(struct tubercular_file_system_information));

        tfsi->free_tubercular_regions = (pow(2, ADDRESS_BITS))-3;
        tfsi->first_free_tubercular_region = 3;
        tfsi->number_of_potatoes = 1;
        tfsi->number_of_tubercular_containers = 2;

        printf("-- Writing TFSI on %lu\n", ftell(fp));
        tam = fwrite(tfsi, sizeof(struct tubercular_file_system_information), 1, fp);

        if(tam!=1){
        	printf("-- Error writing TFSI\n");
        	return -1;
        }

        //Fill TUT
        printf("- Filling TUT...\n");
        struct tubercular_use_entry *tut = 
        (struct tubercular_use_entry *) malloc(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4));
        for(long i = 0; i<(pow(2, ADDRESS_BITS))/4; i++){
            if(i==0) tut[i].info = 0b00011111;
            else tut[i].info = 0b00000000;
        }

        printf("-- Writing TUT on %lu\n", ftell(fp));
        tam = fwrite(tut, sizeof(struct tubercular_use_entry), ((pow(2, ADDRESS_BITS))/4), fp);
        
        if(tam!=pow(2, ADDRESS_BITS)/4){
        	printf("-- Error writing TUT\n");
        	return -1;
        }

        //Fill TDR
        printf("- Initializing the Tubercular Data Region root...\n");
        root = (struct tubercular_container_head*) malloc(sizeof(struct tubercular_container_head));

        root->pathname[0] = '/';
        root->pathname[1] = '\0';

        printf("- Filling root pointers...\n");
        root->files[0] = (uint32_t) 0x00000001;
        root->files[1] = (uint32_t) 0x00000002;
        for(long i = 2; i<TUBERCULAR_CONTAINER_HEAD_PTRS; i++){
        	root->files[i] = (uint32_t) 0x00000000;
        }

        root->next = 0x00000000;

        printf("-- Writing root on %lu\n", ftell(fp));
        tam = fwrite(root, sizeof(struct tubercular_container_head), 1, fp);

        if(tam!=1){
        	printf("-- Error writing root\n");
        	return -1;
        }

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

        printf("-- Writing test directory on %lu\n", ftell(fp));
        tam = fwrite(testdir, sizeof(struct tubercular_container_head), 1, fp);

        if(tam!=1){
        	printf("-- Error writing test directory\n");
        	return -1;
        }

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

        printf("-- Writing test file on %lu\n", ftell(fp));
        tam = fwrite(testfile, sizeof(struct potatoe_head), 1, fp);

        if(tam!=1){
        	printf("-- Error writing test potatoe\n");
        	return -1;
        }

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
        FILE *fp = fopen(data->imagepath, "r+");

        //Reading TFSI
        printf("-- Reading Tubercular File System Information from %lu\n", ftell(fp));
        data->tfsi = (struct tubercular_file_system_information*) malloc(sizeof(struct tubercular_file_system_information));
        tam = fread(data->tfsi, sizeof(struct tubercular_file_system_information), 1, fp);
        //printf("--- %lu of %lu bytes\n\n", tam*sizeof(struct tubercular_file_system_information), sizeof(struct tubercular_file_system_information));
        
        if(tam!= 1/*sizeof(struct tubercular_file_system_information)*/){
            printf("--- Error reading Tubercular File System Information\n");
            if(feof(fp)) printf("--- End Of File\n");
            else if(ferror(fp)) printf("--- Error\n");
            else printf("--- Unknow error\n");
            return -1;
        }

        //Reading TUT
        printf("-- Reading Tubercular Use Table from %lu\n", ftell(fp));
        data->tut = (struct tubercular_use_entry*) malloc(sizeof(struct tubercular_use_entry)*((pow(2, ADDRESS_BITS))/4));
        tam = fread(data->tut, sizeof(struct tubercular_use_entry), (pow(2, ADDRESS_BITS))/4, fp);

        if(tam!=((pow(2, ADDRESS_BITS))/4)){
            printf("--- Error reading first entry of Tubercular Use Table\n");
            if(feof(fp)) printf("--- End Of File\n");
            else if(ferror(fp)) printf("--- Error\n");
            else printf("--- Unknow error\n");
            return -1;
        }

        //Read Root
        printf("-- Reading Root Tubercular Container from %lu\n", ftell(fp));
        tam = fread(root, sizeof(struct tubercular_container_head), 1, fp);
        //printf("--- %lu of %lu bytes\n\n", tam*sizeof(struct tubercular_container_head), sizeof(struct tubercular_container_head));

        if(tam!=1/*sizeof(struct tubercular_container_head)*/){
            printf("--- Error reading first Tubercular Data Region (Root\n");
            if(feof(fp)) printf("--- End Of File\n");
            else if(ferror(fp)) printf("--- Error\n");
            else printf("--- Unknow error\n");
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

    data->file=fopen(data->imagepath, "rw+");
    data->time = time(0);

    //Check if the first Tubercular Data Region is the root
    printf("- Checking root integrity...\n");

    root = (struct tubercular_container_head*) read_tubercular_data(data, 0);

    if(root->pathname[0]!='/' || root->pathname[1]!='\0') {
        printf("-- Incorrect pathname, error\n\n");
        printf("//%c//\n", root->pathname[0]);
        return -1;
    }
    if(data->tut[0].info & 0b00000011 != 0b00000011){
        printf("-- Incorrect TUT entry, error\n\n");
        return -1;
    }

    printf("- Checking root integrity again...\n");
    root = (struct tubercular_container_head*)read_tubercular_data(data, 0x00000000);
    if(root->pathname[0]!='/' || root->pathname[1]!='\0') {
        printf("-- Incorrect pathname, error\n\n");
        printf("//%c//\n", root->pathname[0]);
        return -1;
    }
    
    if(root->files[0] != 0x00000001 || root->files[1] != 0x00000002){
    	printf("-- Pointer 1: %u\n", root->files[0]);
    	printf("-- Pointer 2: %u\n", root->files[1]);
    	printf("-- Incorrect pointers, error\n\n");
    	return -1;
    }

    printf("-- Integrity checking passed\n\n");

    printf("- Checking test folder integrity...\n");
    if(!its_container(data, 0x00000001)) printf("- Mem 1 is not a folder...\n");
    if(!its_container(data, 0x00000002)) printf("- Mem 2 is not a folder...\n");


    printf("- Starting FUSE\n");

    //Init fuse
    return fuse_main(argc, argv, &fuse_ops, data);
}
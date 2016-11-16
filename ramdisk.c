/*
  RAMDISK :  A filesystem that resides on memory and uses fuse system
  
  Author: Durgesh Kumar Gupta (dgupta9@ncsu.edu)

  gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <unistd.h>


// set the block size to 1K bytes
#define BLOCKSIZE 1024
#define MEMORYSIZE 83886080

//hold pointer to ram disk block start
char *memoffset;

// hold all the paths as list
#define MAXPATHLIST 20
char pathlist[MAXPATHLIST][PATH_MAX];
char* blockMap[MAXPATHLIST];
char isDir[MAXPATHLIST];
int fileSize[MAXPATHLIST];

//hold current path
char cwd[PATH_MAX];

static int ramdisk_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return res;
	}
	
	int i=0;
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			//found path
			//if folder set folder props
			if(isDir[i]=='d'){
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
			}else{
				//else file props
				stbuf->st_mode = S_IFREG | 0644;
				stbuf->st_nlink = 1;
				stbuf->st_size = 10240;
			}
			return res;
		}
	}
	
	return -ENOENT;
}

static int ramdisk_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi){
	(void) offset;
	(void) fi;
	
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	
	/* testing directory
	char *pp,*aa;
	pp = strdup(path);
	aa=pp;
	while(*pp != '\0'){
		if(*pp == '/')
			*pp='#';
		pp=pp+1;
	}
	
	filler(buf, (const char *) aa, NULL, 0);
	*/
	
	int i=0;
	// check if directory exists
	int noexist=1;
	for(i=0;i<MAXPATHLIST;i++){
		if((!strcmp(path,pathlist[i]))&&(isDir[i]=='d')){
			noexist=0;
			break;
		}
	}
	
	if((noexist)&&(strcmp(path, "/")))
		return -ENOENT;
	
	char pattern[PATH_MAX];
	strcpy(pattern,path);
	if(pattern[strlen(pattern)-1] != '/')
		strcat(pattern,"/*");
	else
		strcat(pattern,"*");
	for(i=0;i<MAXPATHLIST;i++){
		if(!fnmatch(pattern,pathlist[i],FNM_PATHNAME)){
			int start = strlen(pattern)-1;
			char *filename = pathlist[i];
			filename = filename+start;
			filler(buf, (const char*)filename, NULL, 0);
		}
	}

	return 0;
}

static int ramdisk_mkdir(const char *path, mode_t mode){
	
	int fd = open("/tmp/output",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	write(fd,path,strlen(path));
	
	
	int i,lastNull=-1;
	for(i=0;i<MAXPATHLIST;i++){
		write(fd,"\nPATH:",strlen("\nPATH:"));
		write(fd,pathlist[i],strlen(pathlist[i]));
		if(!strcmp(path,pathlist[i])){
			return -EEXIST;
		}
		if(pathlist[i][0]=='\0'){
			lastNull=i;
			break;
		}
	}
	
	write(fd,"\nLast null\n",strlen("\nLast null\n"));
	char num[11];
	sprintf(num,"%d",lastNull);
	write(fd,num,strlen(num));
	if(lastNull==-1){
		return -ENOSPC;
	}
	
	strcpy(pathlist[i],path);
	isDir[i]='d';
	close(fd);
	return 0;
}

static struct fuse_operations ramdisk_opts={
	.getattr	= ramdisk_getattr,
	.readdir	= ramdisk_readdir,
	.mkdir		= ramdisk_mkdir,
	
};

void init_pathlist(){
	int i=0;
	for(i=0;i<MAXPATHLIST;i++){
		pathlist[i][0]='\0';
		fileSize[i] = 0;
		isDir[i] = 'r';
	}
}


int main(int argc,char *argv[]){
	memoffset = (char *)malloc(MEMORYSIZE);
	memset(memoffset,0,MEMORYSIZE);
	
	init_pathlist();
	
	strcpy(pathlist[0],"/myasdf");
	strcpy(pathlist[1],"/myfile");
	fileSize[1] = 1024;
	strcpy(pathlist[2],"/myfile2");
	fileSize[2] = 2048;
	strcpy(pathlist[3],"/mydir");
	isDir[3]='d';
	strcpy(pathlist[4],"/mydir/myfile3");
	return fuse_main(argc,argv,&ramdisk_opts,NULL);
}
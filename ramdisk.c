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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>

// set the block size to 1K bytes
#define BLOCKSIZE 1024

//#define MEMORYSIZE 1048576
long memorysize = 0;
long blockcount = 0;
//#define BLOCKCOUNT 1024

//hold pointer to ram disk block start
char *memoffset;
char *bitMap;//[BLOCKCOUNT];
int  *nextBlockMap;//[BLOCKCOUNT];

// hold all the paths as list
#define MAXPATHLIST 2000
char pathlist[MAXPATHLIST][PATH_MAX];
int  blockMap[MAXPATHLIST];
char isDir[MAXPATHLIST];
int fileSize[MAXPATHLIST];

//hold current path
char cwd[PATH_MAX];


// log handler
#define LOGFILEPATH "/tmp/ramdisk.log"
#define LOG_ENABLE 1
int logfd;

int usePersist = 0;
char persistPath[PATH_MAX];

static void log_init(){
	if(LOG_ENABLE)
		logfd = open(LOGFILEPATH,O_WRONLY|O_CREAT);
}

static void log_close(){
	if(LOG_ENABLE)
		close(logfd);
}

static void log_write(char *logmsg, ...){
	// copied from http://stackoverflow.com/questions/1442116/how-to-get-date-and-time-value-in-c-program
	if(LOG_ENABLE){
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		dprintf(logfd,"%d-%d-%d %d:%d:%d : ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

		va_list args;
	    va_start(args,logmsg);
	    vdprintf(logfd,logmsg,args);
	    va_end(args);
	    dprintf(logfd,"\n");
	}
}

static int ramdisk_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	log_write("ramdisk_getattr called with path : %s",path);

	memset(stbuf, 0, sizeof(struct stat));
	
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_uid = getuid();
		stbuf->st_gid = stbuf->st_uid;
		return res;
	}
	
	int i=0;
	for(i=0;i<MAXPATHLIST;i++){
		//log_write("IN ramdisk_getattr pathlist[%d]=%s",i,pathlist[i]);
		if(!strcmp(path,pathlist[i])){
			//found path
			//if folder set folder props
			stbuf->st_uid = getuid();
			stbuf->st_gid = stbuf->st_uid;
			if(isDir[i]=='d'){
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				stbuf->st_size = 4096;
			}else{
				//else file props
				stbuf->st_mode = S_IFREG | 0644;
				stbuf->st_nlink = 1;
				stbuf->st_size = fileSize[i];
			}
			log_write("FOUND path [%s] at index : %d",pathlist[i],i);
			return res;
		}
	}
	log_write("Couldn't find path [%s]",path);
	
	return -ENOENT;
}

static int ramdisk_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi){
	(void) offset;
	(void) fi;
	
	log_write("ramdisk_readdir called with path : %s",path);

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
	/*
	int fd = open("/tmp/output",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	write(fd,path,strlen(path));
	*/
	log_write("ramdisk_mkdir called with path : %s",path);
	int i,lastNull=-1;
	for(i=0;i<MAXPATHLIST;i++){
		//write(fd,"\nPATH:",strlen("\nPATH:"));
		//write(fd,pathlist[i],strlen(pathlist[i]));
		if(!strcmp(path,pathlist[i])){
			return -EEXIST;
		}
		if(pathlist[i][0]=='\0'){
			lastNull=i;
			break;
		}
	}
	
	//write(fd,"\nLast null\n",strlen("\nLast null\n"));
	//char num[11];
	//sprintf(num,"%d",lastNull);
	//write(fd,num,strlen(num));
	if(lastNull==-1){
		return -ENOSPC;
	}
	
	strcpy(pathlist[i],path);
	isDir[i]='d';
	//close(fd);
	return 0;
}

static int getfreeBlock(){
	// return block id of next free block
	// or -1 is no free blocks where ENOSPC should be set
	int i=0;
	log_write("getfreeBlock called");
	for(i=0;i<blockcount;i++){
		//log_write("val BITMAP[%d]=%d",i,bitMap[i]);
		if(bitMap[i]==0)
			return i;
	}
	return -1;
}

static void setBlock(int blockNum){
	bitMap[blockNum]=1;
}

static void resetBlock(int blockNum){
	bitMap[blockNum]=0;
}

static int ramdisk_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	int i=0,fileExists=0,index=-1;
	log_write("ramdisk_write called with path : [%s] , buf : [], size: [%d] and offset:[%d]",path,size,offset);
	
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			fileExists=1;
			index=i;
			break;
		}
	}
	
	if(fileExists){
		//file exists
		//based on offset, check which is the starting block
		log_write("ramdisk_write offsetchecksum offset : [%d], filesize : [%d]",offset,fileSize[index]);
		if(offset>fileSize[index])
			return -ENXIO;

		

		//offset = 0;
		int blockOffsetNum = offset/BLOCKSIZE;
		log_write("ramdisk_write and file exists");
		
		int byteWrite=size,nextBlock = blockMap[index];

		//handle offset
		int partialBlock = 0;
		if(offset != 0){
			if(offset%BLOCKSIZE == 0){
				//offset aligns with block 
			}else{
				partialBlock = 1;
			}
			int temp = blockOffsetNum;
			log_write("block offset temo : %d , %d , %d",temp,nextBlock,nextBlockMap[nextBlock]);
			while(temp>1){
				nextBlock = nextBlockMap[nextBlock];
				temp -=1;
			}
			if(offset>=BLOCKSIZE){
				int t = getfreeBlock();
				if(t==-1)
					return -ENOSPC;
				setBlock(t);
				nextBlockMap[nextBlock] = t;
				nextBlock = t;
			}
		}


		while(byteWrite>0){
			if(partialBlock){
				log_write("in ramdisk_write with partial offset to write [%d] as byteWrite:%d, offset : %d", nextBlock, byteWrite,offset);
				char *blockoffset = memoffset + (nextBlock*BLOCKSIZE);
				int partialoffset = offset - ((offset/BLOCKSIZE) * BLOCKSIZE);
				blockoffset += partialoffset;
				strncpy(blockoffset,buf,(BLOCKSIZE-partialoffset));
				partialBlock = 0;
				byteWrite -=(BLOCKSIZE-partialoffset);
				fileSize[index]+=(BLOCKSIZE-partialoffset);
				buf += (BLOCKSIZE-partialoffset);
			}else if(byteWrite<=BLOCKSIZE){
				//write on remaining block
				log_write("in ramdisk_write with only last block [%d] to write as byteWrite:%d",nextBlock, byteWrite);
				char *offset = memoffset  + (nextBlock*BLOCKSIZE);
				strncpy(offset,buf,byteWrite);
				fileSize[index]+=byteWrite;
				byteWrite = 0;
			}else {
				log_write("in ramdisk_write with more block to write as byteWrite:%d",byteWrite);
				log_write("writing a block : [%d]",nextBlock);
				char *offset = memoffset  + (nextBlock*BLOCKSIZE);
				strncpy(offset,buf,BLOCKSIZE);
				int t = nextBlock;
				nextBlock = getfreeBlock();
				if(nextBlock==-1)
					return -ENOSPC;
				setBlock(nextBlock);
				nextBlockMap[t]=nextBlock;
				byteWrite -= BLOCKSIZE;
				fileSize[index]+=BLOCKSIZE;
				buf += BLOCKSIZE;
			}
		}
		//fileSize[index]=(int)size;
		return (int)size;
		/*
		//check the blocks needed for write
		char *next =  memoffset + blockMap[index];
		next = next + blockOffsetNum;
		
		int blockNum=blockMap[index];
		fileSize[index]=size;
		char * buffPtr=(char *)buf;
		while(size>BLOCKSIZE){
			strncpy(next,buffPtr,BLOCKSIZE);
			nextBlockMap[blockNum]=getfreeBlock();
			blockNum = nextBlockMap[blockNum];
			setBlock(blockNum);
			next = memoffset + (BLOCKSIZE*blockNum);
			buffPtr = buffPtr + BLOCKSIZE;
			size = size - BLOCKSIZE;
		}
		
		//fit the final left piece of buffer
		if(size<BLOCKSIZE){
			strncpy(next,buffPtr,size);
		}
		*/
		
	}else{
		// file doesn't exists
		return -ENOENT;
	}
	return 0;
}

static int ramdisk_open(const char *path, struct fuse_file_info *fi){
	log_write("ramdisk_open called with path : %s",path);
	int i=0,fileExists=0;//,index=-1;
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			fileExists=1;
			break;
		}
	}
	
	if(!fileExists)
		return -ENOENT;
		
	
	
	return 0;
}

static int ramdisk_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi){
	//size_t len;
	log_write("ramdisk_read called with path : [%s], size:[%d], offset : [%d]",path,size,offset);
	(void) fi;
	int i,fileExists=0,index=-1;
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			fileExists=1;
			index=i;
			break;
		}
	}
	
	if(fileExists){
		//file exists
		//based on offset, check which is the starting block

		if(offset>fileSize[index])
			return -ENXIO;

		//offset = 0;
		int blockOffsetNum = offset/BLOCKSIZE;
		log_write("ramdisk_read and file exists");
		
		int byteRead=size,nextBlock = blockMap[index];

		//handle offset
		int partialBlock = 0;
		if(offset != 0){
			if(offset%BLOCKSIZE == 0){
				//offset aligns with block 
			}else{
				partialBlock = 1;
			}
			int temp =blockOffsetNum;
			while(temp>0){
				nextBlock = nextBlockMap[nextBlock];
				temp -=1;
			}
		}
		while(byteRead>0){
			if(nextBlock ==-1)
				break;
			if(partialBlock){
				log_write("in ramdisk_read with partial offset to read as byteRead:%d, offset : %d",byteRead,offset);
				char *blockoffset = memoffset + (nextBlock*BLOCKSIZE);
				int partialoffset = offset - ((offset/BLOCKSIZE) * BLOCKSIZE);
				blockoffset += partialoffset;
				strncpy(buf,blockoffset,(BLOCKSIZE-partialoffset));
				partialBlock = 0;
				byteRead -=(BLOCKSIZE-partialoffset);
				buf += partialoffset;
			}else if(byteRead<=BLOCKSIZE){
				//write on remaining block
				log_write("in ramdisk_read with only last block to read as byteRead:%d",byteRead);
				char *offset = memoffset  + (nextBlock*BLOCKSIZE);
				strncpy(buf,offset,byteRead);
				byteRead = 0;
			}else {
				log_write("in ramdisk_read with more block to read as byteRead:%d",byteRead);
				log_write("reading a block : [%d]",nextBlock);
				char *offset = memoffset  + (nextBlock*BLOCKSIZE);
				strncpy(buf,offset,BLOCKSIZE);
				log_write("value read : [[%s]]",buf);
				nextBlock = nextBlockMap[nextBlock];
				byteRead -= BLOCKSIZE;
				buf += BLOCKSIZE;
				log_write("left bytes : %d",byteRead);
			}
		}


/*
		offset = 0;
		int blockOffsetNum = offset/BLOCKSIZE;
		log_write("ramdisk_read called with path : [%s] , buf : [%s]",path,buf);
		
		int byteRead=size,nextBlock = blockMap[index];
		if(byteRead<fileSize[index])
			byteRead=fileSize[index];
		while(byteRead>0){
			if(nextBlock==-1)
				break;
			if(byteRead<=BLOCKSIZE){
				//write on remaining block
				log_write("in ramdisk_read with only last block to write as byteRead:%d",byteRead);
				char *offset = memoffset + (nextBlock*BLOCKSIZE);
				strncpy(buf,offset,byteRead);
				byteRead = 0;
			}else{
				log_write("in ramdisk_read with more block to write as byteRead:%d",byteRead);
				char *offset = memoffset + (nextBlock*BLOCKSIZE);
				strncpy(buf,offset,BLOCKSIZE);
				if(nextBlock==-1)
					return -ENOSPC;
				nextBlock = nextBlockMap[nextBlock];
				byteRead -= BLOCKSIZE;
			}
		}*/
		return ((int)size)-offset;
	}else{
		return -ENOENT;
	}
	return 0;
}

static int ramdisk_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	log_write("ramdisk_mknod called with path : %s",path);
	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable 
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;
	*/
	log_write("ramdisk_mknod called with path : [%s]",path);
	return 0;
}



static int checkInDir(char *dirStr,char *filePath){
	int i=0;
	log_write("checkInDir called with dirStr:[%s] len : %d and filePath:[%s]",dirStr,strlen(dirStr),filePath);
	while((i<strlen(dirStr))&&(i<strlen(filePath))){
		log_write("matching [%d] == [%c]",dirStr[i],filePath[i]);
		if(dirStr[i]!=filePath[i])
			break;
		i+=1;
	}

	log_write("i is :%d",i);
	if(i<strlen(dirStr))
		return 0;
	/*
	while(i<strlen(filePath)){
		if(filePath[i]=='/')
			return 0;
		i+=1;
	}*/
	return 1;
}


static int ramdisk_truncate(const char *pathStr, off_t length)
{
	log_write("ramdisk_truncate called with path : %s",pathStr);

	int i,index=-1,dirExists=0,fileExists=0;

	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(pathStr,pathlist[i])){
			fileExists=1;
			index=i;
			if(isDir[i]=='d')
				return -EISDIR;
		}else if((isDir[i]=='d')&&(checkInDir(pathlist[i],(char *)pathStr))) {
			dirExists = 1;
		}
		if(fileExists&&dirExists)
			break;
	}

	if(checkInDir("/",(char *)pathStr))
		dirExists=1;

	log_write("fileExists=%d and dirExists=%d",fileExists,dirExists);

	if(!dirExists)
		return -ENOENT;

	if(!fileExists){
		// create the file
		log_write("NEW file");
		int lastNull=-1;
		//create a new entry in pathtable
		for(i=0;i<MAXPATHLIST;i++){
			if(pathlist[i][0]=='\0'){
				lastNull=i;
				break;
			}
		}
		if(lastNull==-1)
			return -ENOSPC;
		log_write("Found index %d free",lastNull);
		strcpy(pathlist[lastNull],strdup(pathStr));
		fileSize[lastNull]=0;
		//get free block from bitmap
		int blockNum = getfreeBlock();
		log_write("Found free block at : %d ",blockNum);
		//set the bitmap
		setBlock(blockNum);
		// allocate block
		blockMap[lastNull]=blockNum;
		nextBlockMap[blockNum]=-1;

		return 0;
	}else{
		int size = 	fileSize[index];
		if(length<=BLOCKSIZE){
			if(size>BLOCKSIZE){
				// allocated blocks > 1
				int numBlocks = (size/BLOCKSIZE);
				if(size%BLOCKSIZE==0)
					numBlocks-=1;
				int nextBlock = nextBlockMap[blockMap[index]];
				int *prevBlock;

				while((numBlocks>0)&&(nextBlock!=-1)){
					 resetBlock(nextBlock);
					 prevBlock = &nextBlockMap[nextBlock];
					 nextBlock = nextBlockMap[nextBlock];
					 *prevBlock=-1;
					 numBlocks-=1;
				}
				
			}
		}else{
			int startBlocks = (length/BLOCKSIZE);
			if(length%BLOCKSIZE==0)
				startBlocks-=1;
			int nextBlock = nextBlockMap[blockMap[index]];
			while((startBlocks>0)&&(nextBlock!=-1)){
				nextBlock = nextBlockMap[nextBlock];
				startBlocks-=1;
			}
			int *prevBlock;
			while(nextBlock!=-1){
				resetBlock(nextBlock);
				prevBlock = &nextBlockMap[nextBlock];
				nextBlock = nextBlockMap[nextBlock];
				*prevBlock=-1;
			}
		}
	}
	fileSize[index]=length;
	return 0;
}


static int ramdisk_unlink(const char *path) {
	int i,index=-1,dirExists=0,fileExists=0;
	log_write("ramdisk_unlink called with path : %s",path);
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			fileExists=1;
			index=i;
			if(isDir[i]=='d')
				return -EISDIR;
			break;
		}
	}

	if(!fileExists)
		return -ENOENT;

	log_write("in ramdisk_unlink found path [%s] at index [%d]",path,index);
	int nextBlock = blockMap[index];
	while(nextBlock!= -1){
		resetBlock(nextBlock);
		int *ptrprevblock = &nextBlockMap[index];
		nextBlock = nextBlockMap[index];
		*ptrprevblock=-1;
	}

	blockMap[index]=-1;
	strcpy(pathlist[i],"");
	isDir[index]='r';
	return 0;
}


static int ramdisk_create(const char* pathStr, mode_t mode, struct fuse_file_info *fileInfo){
	log_write("ramdisk_create called with path : %s",pathStr);
	ramdisk_truncate(pathStr,0);
	return 0;
}

static int ramdisk_access(const char* path,int mask){
	int i,index=-1,dirExists=0,fileExists=0;
	log_write("in ramdisk_access with path : %s, mask: %d",path,mask);
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			return 0;
		}
	}
	if(!strcmp(path,"/"))
		return 0;
	return -ENOENT;
}

static int ramdisk_readlink(const char *path, char *buf, size_t size)
{
	log_write("ramdisk_readlink called with path : %s",path);
	return 0;
}

static int ramdisk_rename(const char *from, const char *to)
{
	log_write("ramdisk_rename called with from: [%s] and to [%s]",from,to);
	int i,index=-1,dir=0,fileExists=0;
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(from,pathlist[i])){
			fileExists=1;
			index=i;
			if(isDir[i]=='d')
				dir=1;
		}
	}

	if(dir){
		//special handling for directory	
		log_write("ramdisk_rename called for directory");
	}else{
		if(fileExists)
			strcpy(pathlist[index],to);
		return 0;
	}

	if(!strcmp(from,"/"))
		return -EACCES;

	return -ENOENT;
}

static int ramdisk_rmdir(const char *path)
{
	log_write("ramdisk_rmdir called with path: %s",path);

	//check for subfolder or files inside the dir
	int i=0;
	// check if directory exists
	int exists=0,dir=0,index=-1;
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			if(isDir[i]=='d'){
				exists=1;
				dir=1;
				index=i;
			}
			break;
		}
	}
	
	if(!strcmp(path, "/")){
		log_write("ramdisk_rmdir return ebusy");
		return -EBUSY;
	}

	if(!exists){
		log_write("ramdisk_rmdir return enoent");
		return -ENOENT;
	}
	
	char pattern[PATH_MAX];
	strcpy(pattern,path);
	if(pattern[strlen(pattern)-1] != '/')
		strcat(pattern,"/*");
	else
		strcat(pattern,"*");
	for(i=0;i<MAXPATHLIST;i++){
		if(!fnmatch(pattern,pathlist[i],FNM_PATHNAME)){
			log_write("ramdisk_rmdir return enotempty cause file [%s] exists",pathlist[i]);
			return -ENOTEMPTY;
		}
	}

	// delete the folder
	isDir[index]=='r';
	strcpy(pathlist[index],"");
	pathlist[index][0] = '\0';


	return 0;
}


static int ramdisk_utimens(const char *path, const struct timespec ts[2])
{
	log_write("ramdisk_utimens called with path: %s",path);

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;
int fd = open("/tmp/output-symlink",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}



static int xmp_link(const char *from, const char *to)
{
	int res;
int fd = open("/tmp/output-link",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;
int fd = open("/tmp/output-chmod",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
int fd = open("/tmp/output-chown",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}


static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
int fd = open("/tmp/output-statfs",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */
int fd = open("/tmp/output-release",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	(void) path;
	(void) fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */
int fd = open("/tmp/output-fsync",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

static void ramdisk_destroy(){
	log_write("in ramdisk_destroy !!! with usePersist:%d",usePersist);
	if(usePersist){
		// save content is disk
		log_write("saving content in [%s]",persistPath);
		FILE *dataFile = fopen(persistPath,"wb");
		memorysize /= 1024*1024;
		fwrite(&memorysize,sizeof(int),1,dataFile);
		fwrite(bitMap,1,blockcount,dataFile);
		fwrite(nextBlockMap,sizeof(int),blockcount,dataFile);
		fwrite(blockMap,sizeof(int),MAXPATHLIST,dataFile);
		int i=0;
		for(i=0;i<MAXPATHLIST;i++){
			fwrite(&pathlist[i],1,PATH_MAX,dataFile);
		}
		fwrite(fileSize,sizeof(int),MAXPATHLIST , dataFile);
		fwrite(blockMap,sizeof(int),MAXPATHLIST,dataFile);
		fwrite(isDir,1,MAXPATHLIST,dataFile);

		
		memorysize *= 1024*1024;
		fwrite(memoffset,1,memorysize,dataFile);	
		fclose(dataFile);	
	}
}

static struct fuse_operations ramdisk_opts={
	.getattr	= ramdisk_getattr,
	.readdir	= ramdisk_readdir,
	.mkdir		= ramdisk_mkdir,
	.open		= ramdisk_open,
	.write		= ramdisk_write,
	.read		= ramdisk_read,
	.mknod		= ramdisk_mknod,
	.create		= ramdisk_create,
	.truncate	= ramdisk_truncate,
	.unlink		= ramdisk_unlink,
	.access		= ramdisk_access,
	.rmdir		= ramdisk_rmdir,
	.rename		= ramdisk_rename,
	.readlink	= ramdisk_readlink,
	.utimens	= ramdisk_utimens,
	.destroy 	= ramdisk_destroy,

	.symlink	= xmp_symlink,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.statfs		= xmp_statfs,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
};

void init_pathlist(){
	int i=0;
	for(i=0;i<MAXPATHLIST;i++){
		pathlist[i][0]='\0';
		fileSize[i] = 0;
		isDir[i] = 'r';
	}
	for(i=0;i<blockcount;i++){
		bitMap[i]=0;
	}
	
}

int loads_data(char * path){
	//check file exists
	log_write(" in loads_data File path in params is [%s]",path);
	char currentPath[PATH_MAX];
	getcwd(currentPath,PATH_MAX);
	if(path[0] != '/'){
		//relative path given
		strcat(currentPath,"/");
		strcat(currentPath,path);
		realpath((const char *)strdup(currentPath),currentPath);
	}

	if( access( path, F_OK ) == -1 ) {
	    // file doesn't exist
	    strcpy(persistPath,currentPath);
	    return 1;
	}

	

	//read meta data
	FILE *dataFile = fopen(path,"rb");
	log_write("fopen done");
	fread(&memorysize,sizeof(int),1,dataFile);
	log_write("TOTAL MEMORY : %d",memorysize);
	log_write("fopen memorysize");
	memorysize *= 1024*1024;

	blockcount = memorysize/BLOCKSIZE;
	
	bitMap = (char*) malloc(blockcount);
	fread(bitMap,1,blockcount,dataFile);
	log_write("fopen dataFile");
	nextBlockMap = (int*) malloc(blockcount*(sizeof(int)));
	fread(nextBlockMap,sizeof(int),blockcount,dataFile);
	log_write("fopen nextBlockMap");
	fread(blockMap,sizeof(int),MAXPATHLIST,dataFile);
	log_write("fopen blockMap");
	int i=0;
	for(i=0;i<MAXPATHLIST;i++){
		fread(&pathlist[i],1,PATH_MAX,dataFile);
	}

	log_write("fopen pathlist");
fread(fileSize,sizeof(int),MAXPATHLIST , dataFile);
	fread(blockMap,sizeof(int),MAXPATHLIST,dataFile);
	log_write("fopen blockMap");
	fread(isDir,1,MAXPATHLIST,dataFile);
	log_write("fopen isDir");
	
	log_write("fopen fileSize");
	//lseek to the data address
	memoffset = (char *)malloc(memorysize);
	fread(memoffset,1,memorysize,dataFile);
	log_write("fopen memoffset");
	//read the data and store in memory offset
	fclose (dataFile);
	return 0;
}


int main(int argc,char *argv[]){
	log_init();
	char *datafile;
	if(argc == 3){
		//running without mount file
		memorysize = atoi(argv[2]);
		argv[2][0]='\0';
		argc -=1;
		if(memorysize == 0)
			return -1;
		memorysize *= 1024*1024;

		memoffset = (char *)malloc(memorysize);
		memset(memoffset,0,memorysize);

		blockcount = memorysize/BLOCKSIZE;
		
		bitMap = (char*) malloc(blockcount);
		memset(bitMap,0,blockcount);
		
		nextBlockMap = (int*) malloc(blockcount*(sizeof(int)));
		memset(nextBlockMap,-1,blockcount*(sizeof(int)));

		memset(blockMap,-1,MAXPATHLIST*(sizeof(int)));
		init_pathlist();
	}else{
		//running with mount file
		usePersist = 1;
		memorysize = atoi(argv[2]);
		argv[2][0]='\0';
		argc -=2;
		datafile = strdup(argv[3]);
		if(loads_data(datafile)){
				if(memorysize == 0)
					return -1;
				memorysize *= 1024*1024;

				memoffset = (char *)malloc(memorysize);
				memset(memoffset,0,memorysize);

				blockcount = memorysize/BLOCKSIZE;
				
				bitMap = (char*) malloc(blockcount);
				memset(bitMap,0,blockcount);
				
				nextBlockMap = (int*) malloc(blockcount*(sizeof(int)));
				memset(nextBlockMap,-1,blockcount*(sizeof(int)));

				memset(blockMap,-1,MAXPATHLIST*(sizeof(int)));
				init_pathlist();
		}
	}

	log_write("LOG INITIALIZED, Running fuse");
	int fuse_ret = fuse_main(argc,argv,&ramdisk_opts,NULL);
	log_close();
	return fuse_ret;
}
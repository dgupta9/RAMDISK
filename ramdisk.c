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


// set the block size to 1K bytes
#define BLOCKSIZE 1024
#define MEMORYSIZE 1048576
#define BLOCKCOUNT 1024
//hold pointer to ram disk block start
char *memoffset;
char bitMap[BLOCKCOUNT];
int  nextBlockMap[BLOCKCOUNT];

// hold all the paths as list
#define MAXPATHLIST 20
char pathlist[MAXPATHLIST][PATH_MAX];
int  blockMap[MAXPATHLIST];
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
	/*
	int fd = open("/tmp/output",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	write(fd,path,strlen(path));
	*/
	
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
	for(i=0;i<BLOCKCOUNT;i++){
		if(!bitMap[i])
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
	int fd = open("/tmp/output-write",O_RDWR|O_CREAT);
	write(fd,"\nSTARTLOG\n",strlen("\nSTARTLOG\n"));
	
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
		offset = 0;
		int blockOffsetNum = offset/BLOCKSIZE;
		write(fd,"\nSTARTLOG\n",strlen("\nSTARTLOG\n"));
		
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
			write(fd,"\nSTARTLOG\n",strlen("\nSTARTLOG\n"));
		}
		
	}else{
		// file doesn't exists
		return -ENOENT;
	}
	write(fd,"\nENDLOG\n",strlen("\nENDLOG\n"));
	close(fd);
	return 0;
}

static int ramdisk_open(const char *path, struct fuse_file_info *fi){
	
	int i=0,fileExists=0;//,index=-1;
	for(i=0;i<MAXPATHLIST;i++){
		if(!strcmp(path,pathlist[i])){
			fileExists=1;
			break;
		}
	}
	
	if(fi->flags & O_CREAT){
		//check if O_EXCL flag is set
		if((fi->flags & O_EXCL)&&(fileExists)){
				return -EEXIST;
			}
			int lastNull=-1;
			//create a new entry in pathtable
			for(i=0;i<MAXPATHLIST;i++){
			if(pathlist[i][0]=='\0'){
				lastNull=i;
				break;
			}
			}
			strcpy(pathlist[lastNull],strdup(path));
			fileSize[lastNull]=0;
			//get free block from bitmap
			int blockNum = getfreeBlock();
			//set the bitmap
			setBlock(blockNum);
			// allocate block
			blockMap[lastNull]=blockNum;
			
	}else{
		//create flag not set
		if(!fileExists)
			return -ENOENT;
		
	}
	
	return 0;
}

static int ramdisk_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	//size_t len;
	(void) fi;

	memcpy(buf, "FIX CONTENT", 11);

	return 11;
}

static int ramdisk_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
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
	int fd = open("/tmp/output-mknod",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	return 0;
}

static int randisk_create(const char* pathStr, mode_t mode, struct fuse_file_info *fileInfo){
int fd = open("/tmp/output-create",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	
	int lastNull=-1,i;
	//create a new entry in pathtable
	for(i=0;i<MAXPATHLIST;i++){
	if(pathlist[i][0]=='\0'){
		lastNull=i;
		break;
	}
	}
	strcpy(pathlist[lastNull],strdup(pathStr));
	fileSize[lastNull]=0;
	//get free block from bitmap
	int blockNum = getfreeBlock();
	//set the bitmap
	setBlock(blockNum);
	// allocate block
	blockMap[lastNull]=blockNum;

	return 0;
}


static int ramdisk_truncate(const char *pathStr, off_t length)
{
	int fd = open("/tmp/output-truncate",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;
int fd = open("/tmp/output-access",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = access(path, mask);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;
int fd = open("/tmp/output-readlink",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}



static int xmp_unlink(const char *path)
{
	int res;
int fd = open("/tmp/output-unlink",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;
int fd = open("/tmp/output-rmdir",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = rmdir(path);
	if (res == -1)
		return -errno;

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

static int xmp_rename(const char *from, const char *to)
{
	int res;
int fd = open("/tmp/output-rename",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = rename(from, to);
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

static int xmp_truncate(const char *path, off_t size)
{
	int res;
int fd = open("/tmp/output-truncate",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];
int fd = open("/tmp/output-utimens",O_RDWR|O_CREAT);
	write(fd,"STARTLOG\n",strlen("STARTLOG\n"));
	close(fd);
	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

	res = utimes(path, tv);
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

static struct fuse_operations ramdisk_opts={
	.getattr	= ramdisk_getattr,
	.readdir	= ramdisk_readdir,
	.mkdir		= ramdisk_mkdir,
	.open		= ramdisk_open,
	.write		= ramdisk_write,
	.read		= ramdisk_read,
	.mknod		= ramdisk_mknod,
	.create		= randisk_create,
	.truncate	= ramdisk_truncate,

	
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.utimens	= xmp_utimens,
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
	
	
}


int main(int argc,char *argv[]){
	memoffset = (char *)malloc(MEMORYSIZE);
	memset(memoffset,0,MEMORYSIZE);
	memset(bitMap,0,BLOCKCOUNT);
	memset(nextBlockMap,-1,BLOCKCOUNT*(sizeof(int)));
	memset(blockMap,-1,BLOCKCOUNT*(sizeof(int)));
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
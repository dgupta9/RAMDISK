# RAMDISK

Implements a on memory filesystem, where all the files are stored in memory for faster access and IO. This application will create a new filesystem and mount it to folder. Once mounted, users can store, modify or delete files in it. Before compiling this module make sure fuse library is installed.

## FUSE Filesystem in User Space Installation
```
wget https://github.com/libfuse/libfuse/releases/download/fuse-3.0.0-rc2/fuse-3.0.0rc2.tar.gz
tar -xvf fuse-3.0.0rc2.tar.gz
./configure
make -j8
sudo make install
```

References : [libfuse github page](https://github.com/libfuse/libfuse)

## RAMDISK Installation

- Pull the file from this repository
- Run *make* command
- Create any folder say '/mnt/myramdisk'
- Run *./ramdisk /mnt/myramdisk 512*  where 512 is the size of disk desired in MB

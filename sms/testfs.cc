
#include <iostream>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <string>
#include <string.h>

int main(int argc, char **argv)
{
	std::string directory = "./";

	if ( argc > 1 )
		directory = argv[1];

	struct stat stats;
	if (stat(directory.c_str(), &stats)) {
		int err = errno;
		std::string msg("Unable to stat ");
		msg += directory;
		msg += ": ";
		msg += strerror(err);
		std::cerr << msg << std::endl;
		return(-1);
	}

	printf("stats.st_blksize = %ld (blocksize for file system I/O)\n",
		stats.st_blksize);
	printf("stats.st_blocks = %ld (number of 512B blocks allocated)\n",
		stats.st_blocks);

	struct statfs fsstats;
	if (statfs(directory.c_str(), &fsstats)) {
		int err = errno;
		std::string msg("Unable to statfs ");
		msg += directory;
		msg += ": ";
		msg += strerror(err);
		std::cerr << msg << std::endl;
		return(-1);
	}

	printf("fsstats.f_bsize = %ld (optimal transfer block size)\n",
		fsstats.f_bsize);
	printf("fsstats.f_blocks = %ld (total data blocks in file system)\n",
		fsstats.f_blocks);
	printf("fsstats.f_bfree = %ld (free blocks in fs)\n", fsstats.f_bfree);
	printf("fsstats.f_bavail = %ld (blocks avail non-superuser)\n",
		fsstats.f_bavail);

	return(0);
}


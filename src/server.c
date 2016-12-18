/*
 * server.c 
 * acts as the server operating on a defined port and accepts UDP packets
 * consisting of requests to the MFS contained within printf("\n");
 */
 
#include "mfs.h"
#include "udp.h"
 
int port = 0;
int fd;
char* fileImage;

char header_blocks[3*BSIZE];
struct superblock *sb = (struct superblock *) &header_blocks[0*BSIZE]; 
struct dinode *inodes = (struct dinode *)  &header_blocks[1*BSIZE];
char *bitmap = &header_blocks[2*BSIZE];

int inodesOffset = 1*BSIZE;
int bitmapOffset = 2*BSIZE;
int blksOffset = 3*BSIZE;

int read_bit(int bit) {
	return !!(bitmap[bit/8] & (1 << (7 - bit % 8)));
}

int write_bit(int bit) {
	char *byte = malloc(sizeof(char *));
	int bytesToSeek, i;

	*byte = bitmap[bit/8];
	*byte = (*byte | (1 << (7 - bit % 8)));

	bytesToSeek = bitmap[bit/8];

	//seek to bitmap
	lseek(fd, bitmapOffset, SEEK_SET);
	//seek to correct byte
	for(i=0; i<bytesToSeek; i++) 
		lseek(fd, 1, SEEK_CUR);
	//write bit with byte
	if(write(fd, byte, sizeof(char)) < 0) {
		return -1;
	}
	else {
		return 0;
	}
}

//Runs through inode struct to fin empty inode
int findAvailInum(){
	int i;
	for(i=0; i<sb->ninodes; i++) {
		dinode inode = inodes[i];
		if (inode.type != MFS_REGULAR_FILE && inode.type != MFS_DIRECTORY)
			return i;
		else
			printf("inum %d is taken\n", i);
	}
	return -1; //no inum found
}

//Runs through data bitmap to find free block index, marks and returns the address
int findAvailDataBlock(){
	printf("Find avail data block\n");
	int i;
	for (i = 0; i < 1024; i++){
		if (read_bit(i) == 0){
			write_bit(i);
			return i;
		}
		else
			printf("Data block %d not available\n", i);
	}
	return -1;
}

int displayDirEnt(dinode pinode){
	int i, dirCount;
	char *buffer = malloc(sizeof(char *));
	MFS_DirEnt_t *child = malloc(sizeof(MFS_DirEnt_t *));

	dirCount = 0;
	for (i=0; i<14; i++) {
		if (pinode.addrs[i] != ~0) {
			int offset = pinode.addrs[i];
			printf("--------Block %d Address %d---------\n", i, offset);
			lseek(fd, pinode.addrs[i], SEEK_SET);
			read(fd, buffer, sizeof(MFS_DirEnt_t));
			child = (MFS_DirEnt_t *) buffer;
			dirCount++;
			while (child->inum != -1) {
				printf("DirEnt[%d]: name = %s | inum = %d | address = %d\n", dirCount, child->name, child->inum, offset);
				read(fd, buffer, sizeof(MFS_DirEnt_t));
				offset += sizeof(MFS_DirEnt_t);
				child = (MFS_DirEnt_t *) buffer;
				dirCount++;
			}
		}
		else
			printf("------Block %d Unused------\n", i);
	}
	return 0;
}
		
//Grabs the command line arguments and returns them for use in the main function
void getargs(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s [portnum] [file-system-image]\n", argv[0]);
		exit(1);
	}

	port = atoi(argv[1]);
	fileImage = argv[2];
}

//Simple handshake for whenever a connection is setup with the server
int MFS_Init(char *hostname, int port){
	return 0;	
}

/*MFS_Lookup() takes the parent inode number (which should be the inode number of a directory) 
and looks up the entry name in it. The inode number of name is returned. 
Success: return inode number of name; failure: return -1. 
Failure modes: invalid pinum, name does not exist in pinum.*/
int MFS_Lookup(int pinum, char *name){
	
	int blk, i,j;
	char *buffer = malloc(sizeof(MFS_DirEnt_t));
	MFS_DirEnt_t *child;
	
	if (pinum < 0 || pinum > sb->ninodes)
		return -1; //inode unused, cannot read
	
	//Read in specific inode
	dinode parent = inodes[pinum];
	
	//if it is not a directory inode, fail
	if (parent.type != MFS_DIRECTORY)
		return -1;
	
	
	//for each addr to a datablock 
	for (i = 0; i < 14; i++){
		if(parent.addrs[i] != ~0){
			blk = (parent.addrs[i] / BSIZE) - 4;
			if(read_bit(blk) == 1){
				lseek(fd, parent.addrs[i], SEEK_SET);
			
				for(j = 0; j < 64; j++){
					read(fd, buffer, sizeof(MFS_DirEnt_t));
					child = (MFS_DirEnt_t *)buffer;
				
					if(strcmp(child->name, name) == 0){
						free(buffer);
						return child->inum;
					}
				}
			}
		}
	}	
	
	free(buffer);
	//if name does not exist, return -1.
	return -1;
}


/*MFS_Stat() returns some information about the file specified by inum. 
Upon success, return 0, otherwise -1. The exact info returned is defined by MFS_Stat_t. 
Failure modes: inum does not exist.*/
int MFS_Stat(int inum, MFS_Stat_t *m){
	dinode inode = inodes[inum];

	printf("Stat request received. \n");	
	if (inum < 0 || inum > sb->ninodes)
		return -1; //inum doesn't exist
	
	//set up MFS_Stat struct with info from inode
	m->type = inode.type;
	m->size = inode.size;

	return 0;
}

//Writes a block of size 4096 bytes at the 
//block offset specified by block 
//Returns 0 on success, -1 on failure 
//Failure modes: invalid inum, invalid block, not a 
//regular file (because you can't write to directories)
//DONE
int MFS_Write(int inum, char *buffer, int block){
	unsigned int blkAddr, freeBlkOffset;
	int i;	

	if (inum < 0 || inum > sb->ninodes)
		return -1; //inode unused, cannot read
	if (block < 0 || block > sb->nblocks)
		return -1; //invalid block
    	
	dinode inode = inodes[inum];
	if (inode.type != MFS_REGULAR_FILE)
		return -1; //can't write to directories

	blkAddr = inode.addrs[block];

	if (blkAddr == ~0) {

		i = findAvailDataBlock();
		if (i < 0) 
			return -1; //no avail data block	

		//seek to free block;
		freeBlkOffset = (blksOffset + (i*BSIZE));
		lseek(fd, freeBlkOffset, SEEK_SET);
		inode.addrs[block] = freeBlkOffset;
		inode.size += BSIZE;
		lseek(fd, inodesOffset, SEEK_SET);

		int l = 0;
		for(l = 0; l < inum; l++){
			lseek(fd, sizeof(dinode), SEEK_CUR);
		}
		write(fd, (char *)&inode, sizeof(dinode));
		blkAddr = freeBlkOffset;
	}

	//seek to offset depending on which block
	if (lseek(fd, blkAddr, SEEK_SET) < 0)
		return -1; //lseek failed
	
	//now write from buffer to that block
	if (write(fd, buffer, BSIZE) < 0)
		return -1; //read failed

	fsync(fd);
	return 0;
}


//Reads a block specified by block into the buffer 
//from file specified by inum 
//The routine should work for either a file or directory;
//directories should return data in the format specified by MFS_DirEnt_t
//Success: 0, failure: -1 
//Failure modes: invalid inum, invalid block
int MFS_Read(int inum, char *buffer, int block){
	
	unsigned int dataOffset;
	dinode inode = inodes[inum];

	if (inum < 0 || inum > sb->ninodes)
		return -1; //invalid inode index
	if (block < 0 || block > 14)
		return -1; //invalid block index
	if (inode.type == 0)
		return -1; //invalid inode
	if (inode.addrs[block] == ~0)
		return -1; //invalid block

	dataOffset = inode.addrs[block];

	if (lseek(fd, dataOffset, SEEK_SET) < 0)
		return -1;

	//read the entry into the buffer
	if (read(fd, buffer, sizeof(MFS_DirEnt_t)) < 0)
		return -1; //read failed
		
	return 0;
}

//Makes a file (type == MFS_REGULAR_FILE) or directory (type == MFS_DIRECTORY) 
//in the parent directory specified by pinum of name name
//Returns 0 on success, -1 on failure
//Failure modes: pinum does not exist, or name is too long
//If name already exists, return success
int MFS_Creat(int pinum, int type, char *name) {
	dinode pinode = inodes[pinum];
	MFS_DirEnt_t *child;
	int childOffset, dirCount; 
	int blkExists, newBlk, newBlockUsed, i;
	
	printf("Creat request received. \n");	

	//***************************Error Checking***************************	
	
	if (pinum < 0 || pinum > sb->ninodes) {
		printf("Creat Failed: inode unused, cannot read\n");
		return -1; //inode unused, cannot read
	}
  	if (strlen(name) > 60) {
		printf("Creat Failed: name is too long\n");
		return -1; //name is too long
	}	
	if (pinode.type != MFS_DIRECTORY) {
		printf("Creat Failed: parent not a directory\n");
		return -1; //parent not a directory
	}

	//********************************************************************
	
	char *buffer = malloc(sizeof(char *));

	printf("Before\n\n");
	displayDirEnt(pinode);
	printf("\n\n");


	//*************************Search for Same Name***********************
	
	//search through existing MFS_DirEnt's for same name
	printf("Searching for same name...\n\n");
	dirCount = 0;
	blkExists = -1;
	newBlk = -1;
	for (i=0; i<14; i++) {
		if (pinode.addrs[i] != ~0) { //if block exists
			printf("%d block exists\n", i);
			//get block
			childOffset = pinode.addrs[i];
			lseek(fd, childOffset, SEEK_SET);
			read(fd, buffer, sizeof(MFS_DirEnt_t));
			child = (MFS_DirEnt_t *) buffer;
			dirCount++;
			printf("Child inum = %d\n", child->inum);
			while (child->inum != -1) {
				if (strcmp(child->name, name) == 0) {
					printf("name matches at %d MFS_DirEnt in block %d\n", dirCount, i);
					return 0; //name already exists, return success
				}
						read(fd, buffer, sizeof(MFS_DirEnt_t));
				child = (MFS_DirEnt_t *) buffer;
				printf("Child inum = %d\n", child->inum);
				if (child->inum == -1) {
					printf("empty DirEnt at %d MFS_DirEnt in block %d\n", dirCount, i);
					blkExists = i;
				}
				else
					dirCount++;

				if(dirCount == 64) //block full of DirEnt's
					break;
			}
		}
		else {
			printf("pinode.addrs[%d] block not allocated\n", i); 
			newBlk = i;
		}
	}
	printf("\nDone!\n\n");

	printf("Finding place for DirEnt...\n\n");
	newBlockUsed = 0;
	if (blkExists == -1) { //no allocated block with available space
		if (newBlk == -1) { //no new block to allocate
			printf("Creat Failed: no space available\n");
			return -1; //no space
		}
		else { //allocate a new block for 
			printf("Allocate new block\n");
			i = findAvailDataBlock();//find 4-KB directory block
			if (i < 0) {
			    printf("Creat Failed: no available data blk\n");	
				return -1; //no avail data blk	
			}
			//seek to free block;
			pinode.addrs[newBlk] = (blksOffset + (i*BSIZE));
			pinode.size += BSIZE;
			newBlockUsed = 1;
		}
	}
	printf("Block %d has spot for new DirEnt\n\n", blkExists);
		
	//********************************************************************

	int newInum, dirEntOffset, freeBlkOffset;
	dinode newInode;
	MFS_DirEnt_t *emptyDir, *dirEnt, *currEnt;
	emptyDir = malloc(sizeof(MFS_DirEnt_t));
	dirEnt = malloc(sizeof(MFS_DirEnt_t));
	currEnt = malloc(sizeof(MFS_DirEnt_t));

	newInum = 0;
	dirEntOffset = 0;
	freeBlkOffset = 0;

	//*************************Create New DirEnt**************************
	
	strcpy(dirEnt->name, name); //set name to given name
	
	newInum = findAvailInum();
	printf("Available inum found = %d\n\n", newInum);

	if (newInum == -1) {
		printf("Creat Failed: no available inodes\n");
		return -1; //no available inodes
	}
	dirEnt->inum = newInum;

	if (newBlockUsed) {
		dirEntOffset = pinode.addrs[newBlk];
		lseek(fd, dirEntOffset, SEEK_SET);
		printf("Writing DirEnt in new allocated block %d at address %d\n", newBlk, dirEntOffset);
	}
	else {
		dirEntOffset = pinode.addrs[blkExists];
		printf("pinode.addrs[%d] = %d\n", blkExists, pinode.addrs[blkExists]);
		lseek(fd, dirEntOffset, SEEK_SET);
		read(fd, buffer, sizeof(MFS_DirEnt_t));
		int offset = dirEntOffset;
		printf("Now at %d\n", offset);
		currEnt = (MFS_DirEnt_t *) buffer;
		while (currEnt->inum != -1) { //MFS_DirEnt taken
				read(fd, buffer, sizeof(MFS_DirEnt_t)); //read another DirEnt
				offset += sizeof(MFS_DirEnt_t);
				printf("Now at %d\n", offset);
				currEnt = (MFS_DirEnt_t *) buffer;
		}
		printf("Writing DirEnt in block %d at address %d\n", blkExists, offset);
	}

	printf("DirEnt inum = %d, offset = %d\n", dirEnt->inum, (int) lseek(fd, -sizeof(MFS_DirEnt_t), SEEK_CUR));
	write(fd, (char *) dirEnt, sizeof(MFS_DirEnt_t)); //write new MFS_DirEnt
	printf("Done!\n\n");

	printf("After MFS_DirEnt_t made\n\n");
	fsync(fd);
	displayDirEnt(pinode);
	
	newInode = inodes[newInum]; //set up new inode with inum

	//********************************************************************

	//**************************Create Directory**************************

	if(type == MFS_DIRECTORY) {
 		printf("\n\nCreating MFS_DIRECTORY...\n\n");
		newInode.type = MFS_DIRECTORY; 
		
		i = findAvailDataBlock();//find 4-KB directory block
		printf("Avail Data block = %d\n", i);
		if (i < 0) {
			printf("Creat Failed: no available data blk\n");\
			return -1; //no avail data blk	
		}

		//seek to free block;
		freeBlkOffset = (blksOffset + (i*BSIZE));
		newInode.addrs[0] = freeBlkOffset;
		printf("free block offset = %d\n", freeBlkOffset);
		lseek(fd, freeBlkOffset, SEEK_SET);

		//write empty DirEnt's
		emptyDir->inum = -1; //entries not yet in use
		for(i=0; i<64; i++) //fill block with unused DirEnt's
			write(fd, (char *) emptyDir, sizeof(MFS_DirEnt_t));

		int endByte = lseek(fd, 0, SEEK_CUR);
		printf("endBytes = %d\n", endByte);
		
		//set up self and parent MFS_DirEnt's	
		int selfOffset;
		//seek to first DirEnt and set up child
		selfOffset = newInode.addrs[0];
		endByte = lseek(fd, selfOffset, SEEK_SET);
		printf("First DirEnt '.' at %d (should = %d)\n", endByte, freeBlkOffset);
		read(fd, buffer, sizeof(MFS_DirEnt_t));
		dirEnt = (MFS_DirEnt_t *) buffer;
		strcpy(dirEnt->name, ".");
		dirEnt->inum = newInum;
		write(fd, (char *) dirEnt, sizeof(MFS_DirEnt_t));
		//next DirEnt is parent, set up parent
		read(fd, buffer, sizeof(MFS_DirEnt_t));
		dirEnt = (MFS_DirEnt_t *) buffer;
		strcpy(dirEnt->name, "..");
		dirEnt->inum = pinum;
		write(fd, (char *) dirEnt, sizeof(MFS_DirEnt_t));
	
	}
    //******************************************************************

	//************************Create Regular File***********************
	
	if(type == MFS_REGULAR_FILE) {
		printf("\n\nCreating REGULAR_FILE...\n\n");
		newInode.type = MFS_REGULAR_FILE;		
		newInode.size = 0;

	    for(i=0; i<14; i++) 
			newInode.addrs[i] = ~0;

		newInode.addrs[0] = findAvailDataBlock();
		printf("AvailDataBlock = %d\n", newInode.addrs[0]);
		write_bit(newInode.addrs[0]);
 
	}

	//******************************************************************

	//**************************Write New Inode*************************

	int inodeOffset = inodesOffset + (newInum*sizeof(dinode));	
	lseek(fd, inodeOffset, SEEK_SET);

	write(fd, (char *)&newInode, sizeof(dinode));
	printf("Done!\n");

	//******************************************************************
	
	printf("After\n\n");
	displayDirEnt(pinode);
	printf("\n\n");

	fsync(fd);
	return 0;
}

/*MFS_Unlink() removes the file or directory name from the directory 
specified by pinum. 
0 on success, -1 on failure. 
Failure modes: pinum does not exist, directory is NOT empty. 
Note that the name not existing is NOT a failure by our definition .*/
int MFS_Unlink(int pinum, char *name){
	printf("Unlink request recieved \n");	
	
	printf("Checking for valid pinum\n");
	if (pinum < 0 || pinum > sb->ninodes)
		return -1; //inode unused, cannot read
	
	printf("Reading parent inode\n");
	//Read in specific inode
	dinode parent = inodes[pinum];
	
	printf("Check for directory code: %d\n", parent.type);
	//if it is not a directory inode, fail
	if (parent.type != MFS_DIRECTORY)
		return -1;
		
	int i, j, ii, jj;
	MFS_DirEnt_t *child;
	MFS_DirEnt_t *inodeChild;
	dinode inode;
	char *childBuffer = malloc(sizeof(MFS_DirEnt_t));
	char *inodeBuffer = malloc(sizeof(MFS_DirEnt_t));
	
	printf("Beginning cycle through all datablocks --- \n");
	//Cycle through all the addresses, looking for name
	for (i = 0; i < 14; i++){
	
		printf("Looking at data block %d \n", i);
		if(parent.addrs[i] != ~0){
		
			printf("Reading block %d\n", parent.addrs[i]);
			int blk = (parent.addrs[i] / BSIZE) - 4;
			if(read_bit(blk) == 1){
				printf("seeking to block\n");
				lseek(fd, parent.addrs[i], SEEK_SET);
			
				printf("Searching for name: %s \n", child->name);
				for(j = 0; j < 64; j++){
				
					read(fd, childBuffer, sizeof(MFS_DirEnt_t));
					child = (MFS_DirEnt_t *)childBuffer;
				
					if(strcmp(child->name, name) == 0){
					
						//if the inode is to a directory, check to see if is empty
						printf("Name found!\n Inode type: %d  \n", inode.type);
						inode = inodes[child->inum];
						if (inode.type == MFS_DIRECTORY){
						
							//run through each address
							for (ii = 0; ii < 14; ii++){
							
								printf("Directory detected, looking for entries within\n");
								if (inode.addrs[ii] != ~0){
								
									lseek(fd, inode.addrs[ii], SEEK_SET);
									for(jj = 0; jj < 64; jj++){
										read(fd, inodeBuffer, sizeof(MFS_DirEnt_t));
										inodeChild = (MFS_DirEnt_t *)inodeBuffer;
					
										//if it is not empty, return -1
										if(inodeChild->inum != -1){
											free(childBuffer);
											free(inodeBuffer);
											return -1;
										}
									}
								}
							}
						}
			
						printf("Erasing inum \n");
						//else erased the directory entry, write to file, and break
						child->inum = -1;
	
						printf("writing to file \n");
						lseek(fd, parent.addrs[i], SEEK_SET);
						write(fd, (char *)child, sizeof(MFS_DirEnt_t));
						fsync(fd);
	
						break; 
					}
				}
			}
		}
	}	
	
	printf("Exiting \n");
	free(childBuffer);
	free(inodeBuffer);
	return 0;
}

//Main function that sets up the server and waits for packets
int main(int argc, char *argv[])
{
	int i, j;		
	getargs(argc, argv);  //grab the command line arguments for use in the server
	printf("Port: %d, File Image: %s\n", port, fileImage);

	if(access(fileImage, F_OK) != -1) { //image exists
		fd = open(fileImage, O_RDWR);	
		pread(fd, header_blocks, (3*BSIZE), BSIZE);
	} 
	else { 
		//image doesn't exist, create 
		fd = open(fileImage, O_CREAT, O_RDWR);
		pread(fd, header_blocks, (3*BSIZE), BSIZE);
		
		//default file system sizing
		sb->size = 1028;
		sb->nblocks = 1024;
		sb->ninodes = 64;
		
		//set inodes to have unused addresses
		for(i=0; i<sb->ninodes; i++) {
			for(j=0; j<14; j++) {
				inodes[i].addrs[j] = ~0;
			}
		}
		
		inodes[0].type = MFS_DIRECTORY;
		inodes[0].size = BSIZE;
		inodes[0].addrs[0] = blksOffset;
		
		//allocate first data block with DirEnt
		MFS_DirEnt_t firstBlock[64];
		
		//set up entry for . and .. pointing to inode 0 (root)
		strncpy(firstBlock[0].name,".", 60);
		firstBlock[0].inum = 0;
		strncpy(firstBlock[1].name,"..", 60);
		firstBlock[1].inum = 0;
		
		//initialize the rest of the block to -1 (unused)
		int index = 0;
		for (index = 2; index < 64; index++)
			firstBlock[index].inum = -1;	
			
		//Write the inode block and first data block to file
		lseek(fd, blksOffset, SEEK_SET);
		write(fd, (char *)&firstBlock, BSIZE);
		fsync(fd);
		
		lseek(fd, inodesOffset, SEEK_SET);
		write(fd, (char *)&inodes, BSIZE); 
	}

	struct sockaddr_in client;
	message msg;
	response rsp;

	//Open the port specified by the parameters
	int serverFd = UDP_Open(port);
	
	//Infinite read loop for interpreting messages
	while(1) {
		//Read in a message on the open port
		UDP_Read(serverFd, &client, (char *)&msg, sizeof(msg));
		
		//Interpret the response and launch the file system command
		if (strcmp(msg.cmd, "init") == 0)
			rsp.rc = MFS_Init("localhost", port);	
		else if (strcmp(msg.cmd, "lookup") == 0)
			rsp.rc = MFS_Lookup(msg.inum, msg.name);
		else if (strcmp(msg.cmd, "stat") == 0)
			rsp.rc = MFS_Stat(msg.inum, &rsp.stat);	
		else if (strcmp(msg.cmd, "write") == 0)
			rsp.rc = MFS_Write(msg.inum, (char *)msg.block, msg.blocknum);
		else if (strcmp(msg.cmd, "read") == 0)
			rsp.rc = MFS_Read(msg.inum, (char *)rsp.block, msg.blocknum);	
		else if (strcmp(msg.cmd, "create") == 0)
			rsp.rc = MFS_Creat(msg.inum, msg.type, msg.name);	
		else if (strcmp(msg.cmd, "unlink") == 0)
			rsp.rc = MFS_Unlink(msg.inum, msg.name);
		else if (strcmp(msg.cmd, "shutdown") == 0)
			break;
		else {
			printf("Unknown command\n");
			rsp.rc = -1;
		}
		
		//return the message as completed by the command
		UDP_Write(serverFd, &client, (char *)&rsp, sizeof(rsp));
	}
	
	//Shutdown code, fsync, send a return message, close the port, and exit
	printf("Server shutting down...\n");
	
	fsync(fd);
	close(fd);
	
	rsp.rc = 0;
	UDP_Write(serverFd, &client, (char *)&rsp, sizeof(rsp));
	UDP_Close(serverFd);
	
	exit(0);
}


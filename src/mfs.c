/*
 * mfs.c
 * The library file responsible for wrapping any File IO 
 * to send to the NFS file server using UDP packets
 */

#include "mfs.h"
#include "udp.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

int serverPort = -1;
struct sockaddr_in server; //socketaddr used to keep track of the server we are currently using
struct sockaddr_in otherSock;

// Encapsulation of the UDP packet sending functionality
response sendUDPPacket(message payload){
	
	int fd;
	response resp;
	fd_set set;
  	struct timeval timeout;	
	
	if ((fd = UDP_Open(0)) == -1)
		exit(1);
		
	// Initialize the file descriptor set
	FD_ZERO (&set);
	FD_SET (fd, &set);
	
	// Initialize the timeout to 5.0 seconds
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	resp.rc = -1;
		
	if ((UDP_Write(fd, &server, (char *)&payload, sizeof (payload))) == -1){
		printf("Error: No bytes sent");
		exit(1);
	}
		
	while(1){
		int ready = select(fd+1, &set, NULL, NULL, &timeout);
		if(ready == 1){
			if((UDP_Read(fd, &otherSock, (char *)&resp, sizeof (response))) == -1){
				printf("Error: No bytes received");
				exit(1);
			}
			break; //break when sometihng is read
		}
		else{
			printf("Read timeout\n");
			
			// Re-initialize the file descriptor set
			FD_ZERO (&set);
			FD_SET (fd, &set);
	
			// Re-initialize the timeout to 5.0 seconds
			timeout.tv_sec = 5;
			timeout.tv_usec = 0;
		}
	}
	
	UDP_Close(fd);
	
	return resp;
}

//Takes a host name and port number and uses those 
//to find the server exporting the file system
int MFS_Init(char *hostname, int port){
	
	//send a message to make sure connection works
	message msg;
	response resp;
	
	//Setup socket addr for use with this new server
	serverPort = port;
	
	if ((UDP_FillSockAddr(&server, hostname, port)) == -1){
		printf("port fill failure \n");
		exit(1);
	}
	
	strncpy(msg.cmd, "init\0", 24);
	resp.rc = -1;
		
	
	//send the message
	resp = sendUDPPacket(msg);

	//return the return code to see if connection was made
	return resp.rc;
}
 
//Takes the parent inode number (which should be the 
//inode number of a directory) and looks up the entry name in it 
//The inode number of name is returned 
//Success: return inode number of name; failure: return -1 
//Failure modes: invalid pinum, name does not exist in pinum
int MFS_Lookup(int pinum, char *name){
	
	//Setup lookup message struc
	message msg;
	response resp;
	
	strncpy(msg.cmd, "lookup", 24);
	msg.inum = pinum;
	msg.type = MFS_DIRECTORY;
	strncpy(msg.name, name, 64);
	
	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	
	//return the inum in the response, -1 if nothing is found
	return resp.rc;
}

 
//Returns some information about the file specified by inum
//Upon success, return 0, otherwise -1 
//The exact info returned is defined by MFS_Stat_t 
//Failure modes: inum does not exist
int MFS_Stat(int inum, MFS_Stat_t *m){
	
	//Setup lookup message struct
	message msg;
	response resp;
	
	strncpy(msg.cmd, "stat", 24);
	msg.inum = inum;
	 
	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	
	//Pass along the response MFS_Stat
	*m = resp.stat;
	
	if(resp.rc == -1)
		return -1;
	else
		return 0;
}

//Writes a block of size 4096 bytes at the 
//block offset specified by block 
//Returns 0 on success, -1 on failure 
//Failure modes: invalid inum, invalid block, not a 
//regular file (because you can't write to directories)
int MFS_Write(int inum, char *buffer, int block){
	
	//Setup lookup message struct
	message msg;
	response resp;
	
	strncpy(msg.cmd, "write", 24);
	msg.inum = inum;
	strncpy(msg.block, buffer, 4096);
	msg.blocknum = block;
	
	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	
	return resp.rc;
}

//Reads a block specified by block into the buffer 
//from file specified by inum 
//The routine should work for either a file or directory;
//directories should return data in the format specified by MFS_DirEnt_t
//Success: 0, failure: -1 
//Failure modes: invalid inum, invalid block
int MFS_Read(int inum, char *buffer, int block){
	
	//Setup lookup message struct
	message msg;
	response resp;
	
	strncpy(msg.cmd, "read", 24);
	msg.inum = inum;
	msg.blocknum = block;
	
	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	strncpy(buffer, resp.block, 4096);
	
	return resp.rc;
}
 
//Makes a file (type == MFS_REGULAR_FILE) or directory (type == MFS_DIRECTORY) 
//in the parent directory specified by pinum of name name
//Returns 0 on success, -1 on failure
//Failure modes: pinum does not exist, or name is too long
//If name already exists, return success
int MFS_Creat(int pinum, int type, char *name){
        
        //Setup lookup message struct
	message msg;
	response resp;
	
	strncpy(msg.cmd, "create", 24);
	msg.inum = pinum;
	msg.type = type;
	strncpy(msg.name, name, 64);
	
	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	
	return resp.rc;
}

//Removes the file or directory name from the directory specified by pinum
//Returns 0 on success, -1 on failure
//Failure modes: pinum does not exist, directory is NOT empty
//Note that the name not existing is NOT a failure by our definition
int MFS_Unlink(int pinum, char *name){
	 
	//Setup lookup message struct
	message msg;
	response resp;
	
	strncpy(msg.cmd, "unlink", 24);
	msg.inum = pinum;
	strncpy(msg.name, name, 64);
	
	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	
	return resp.rc;
}

//Tells the server to force all of its data structures to disk and shutdown 
//by calling exit(0)
//This interface will mostly be used for testing purposes
int MFS_Shutdown(){

	 //Setup lookup message struct
	message msg;
	response resp;
	
	strncpy(msg.cmd, "shutdown", 24);

	resp.rc = -1;
	
	//send the message
	resp = sendUDPPacket(msg);
	
	return resp.rc;
}


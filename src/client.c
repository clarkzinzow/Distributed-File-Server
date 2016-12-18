/*
 *	client.c
 *	program used for testing our NFS server 
 */

#include <stdio.h>
#include "mfs.h"
#include "udp.h"

int main(int argc, char *argv[])
{
	int i;
	char *hostname = "localhost";
	int port = 12345;

	printf("I am the client!\n");
	
	i = MFS_Init(hostname, port);
	
	i = MFS_Creat(0, MFS_REGULAR_FILE, "appear");
	printf("Returned creat value = %d\n",i);
	
	i = MFS_Shutdown();
	printf("Returned shutdown value = %d\n",i);
	
	return 0;
}

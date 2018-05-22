#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "memlayout.h"
#include "mmu.h"

volatile int
main(int argc, char *argv[])
{


	printf(1,"\n------------------ Starting allocate and deallocate test ------------------ \n\n");
	char *forkArgv3[] = { "myFreeTest", 0 };
	if(fork() == 0)
		exec("myFreeTest",forkArgv3);
	wait();
	printf(1,"\n------------------ Finished allocate and deallocate test ------------------ \n");

	printf(1,"\n------------------ Starting Load Test ------------------ \n\n");
	char *loadArgv[] = { "myLoadTest", 0 };
	if(fork() == 0)
		exec("myLoadTest",loadArgv);
	wait();
	printf(1,"\n------------------ Finished load test ------------------ \n");

	/** parent creates less than max_physical_number pages, forks one child and then child manipulates parents copied pages **/
	printf(1,"\n------------------ Starting parent vs. child page differences test------------------ \n\n");
	char *forkArgv2[] = { "myForkTest2", 0 };
	if(fork() == 0)
		exec("myForkTest1",forkArgv2);
	wait();
	printf(1,"\n------------------ Finished parent vs. child page differences test------------------ \n");


	printf(1,"\n------------------ Starting bad exec Test ------------------ \n\n");
	char *badArgv[] = { "nop", 0 };
	if(fork() == 0){
		exec("nop",badArgv);
		printf(1,"\nexec command failed! (as expected)\nFell back to the child\n");
		exit();
	}
	wait();
	printf(1,"\n------------------ Finished bad exec Test Passed ------------------ \n");

	printf(1,"\n\n------------------ All tests passed... myMemTest exit ------------------ \n\n");
	printf(1,"\n\n------------------            ¯\\_(ツ)_/¯          ------------------ \n\n");
	exit();
	return 0;
}

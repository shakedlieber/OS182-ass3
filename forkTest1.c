#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "memlayout.h"
#include "mmu.h"

#define NUMBER_OF_ALLOCS 20
#define NUMBER_OF_ALLOCSLESS 11
char* data[NUMBER_OF_ALLOCS];
volatile int
main(int argc, char *argv[])
{


	int i = 0;

	/** allocate #less pages to the father **/
	for (i = 0; i < NUMBER_OF_ALLOCSLESS ;)
	{
		data[i] = sbrk(PGSIZE);
		data[i][0] = 0 + i;
		data[i][1] = 10 + i;
		data[i][2] = 20 + i;
		data[i][3] = 30 + i;
		data[i][4] = 40+i;
		data[i][5] = 50+i;
		data[i][6] = 60+i;
		data[i][7] = 70+i;
		printf(1, "allocated new page #%d at address: %x\n", i, data[i]);
		i++;
	}
	/** fork son ! **/
	if (fork() == 0)
	{
		printf(1, "\n===== Son Code adds allocations and changes previous data =====\n");
		/** allocate extra pages so the father doesn't know about them **/
		for(i=NUMBER_OF_ALLOCSLESS; i<NUMBER_OF_ALLOCS;i++ ){
			data[i] = sbrk(PGSIZE);
			data[i][0] = 0 + i;
			data[i][1] = 10 + i;
			data[i][2] = 20 + i;
			data[i][3] = 30 + i;
			data[i][4] = 40+i;
			data[i][5] = 50+i;
			data[i][6] = 60+i;
			data[i][7] = 70+i;
			printf(1, "allocated new page #%d at address: %x\n", i, data[i]);
		}
		/** change previously received data from the father ! **/
		for (i = 0; i < NUMBER_OF_ALLOCS;)
		{
			data[i][0] = 10 + data[i][0];
			data[i][1] = 10 + data[i][1];
			data[i][2] = 10 + data[i][2];
			data[i][3] = 10 + data[i][3];
			data[i][4] = 10 + data[i][4];
			data[i][5] = 10 + data[i][5];
			data[i][6] = 10 + data[i][6];
			data[i][7] = 10 + data[i][7];
			i++;
		}
		for (i = 0; i < NUMBER_OF_ALLOCS; i++)
		{
			printf(1, "page #%d => %d, %d, %d, %d, %d, %d, %d, %d\n", i, data[i][0],
			       data[i][1], data[i][2], data[i][3], data[i][4], data[i][5], data[i][6], data[i][7], data[i][8]);
		}
		exit();
	}

	wait();

	printf(1, "\n\n===== Father Code after fork Print all current Data =====\n");

	for(i=0;i< NUMBER_OF_ALLOCSLESS;i++)
	{
		printf(1, "page #%d => %d, %d, %d, %d, %d, %d, %d, %d\n", i, data[i][0],
		       data[i][1], data[i][2], data[i][3], data[i][4], data[i][5], data[i][6], data[i][7], data[i][8]);
	}

	exit();
	return 0;
}

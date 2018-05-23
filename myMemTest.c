
#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "memlayout.h"
#include "mmu.h"

#define N_PAGES 24

char* data[N_PAGES];

volatile int main(int argc, char *argv[]) {

	int i = 0;
	int n = N_PAGES;

	for (i = 0; i < n ;)
	{
		data[i] = sbrk(PGSIZE);
		data[i][0] = 00 + i;
		data[i][1] = 10 + i;
		data[i][2] = 20 + i;
		data[i][3] = 30 + i;
		data[i][4] = 40 + i;
		data[i][5] = 50 + i;
		data[i][6] = 60 + i;
		data[i][7] = 70 + i;
		printf(1, "allocated new page #%d at address: %x\n", i, data[i]);
		i++;
	}

	printf(1,"\nIterate through pages seq:\n");

	int j;
	for(j = 1; j < n; j++)
	{
		printf(1,"j:  %d\n",j);

		for(i = 0; i < j; i++) {
			data[i][10] = 2; // access to the i-th page
			printf(1,"%d, ",i);
		}
		printf(1,"\n");
	}

	int k;
	printf(1, "\nGo through same 8 pages and different 8 others\n");
	for(j = 0; j < 8; j++){
		for(i = 20; i < 24; i++) {
			data[i][10] = 1;
			printf(1,"%d, ",i);
		}
		printf(1,"\n");
		switch (j%4) {
		case 0:
			for(k = 0; k < 4; k++) {
				data[k][10] = 1;
				printf(1,"%d, ",k);
			}
			break;
		case 1:
			for(k = 4; k < 8; k++) {
				data[k][10] = 1;
				printf(1,"%d, ",k);
			}
			break;
		case 2:
			for(k = 8; k < 12; k++) {
				data[k][10] = 1;
				printf(1,"%d, ",k);
			}
			break;
		case 3:
			for(k = 12; k < 16; k++) {
				data[k][10] = 1;
				printf(1,"%d, ",k);
			}
			break;
		}
		
		// data[j][10] = 0;
		// printf(1,"%d, ",j);
		printf(1,"\n");
	}
	exit();
	return 0;
}
	
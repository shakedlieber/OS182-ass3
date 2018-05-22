#include "param.h"
#include "types.h"
#include "user.h"

#define PGSIZE 4096
#define BUFFERS_SIZE 1024
#define BUFFERS_NUM (PGSIZE * 20) / BUFFERS_SIZE
#define POLICY_BUFFERS_NUM 20

void
malloc_test(int num) 
{
  int* buffers[BUFFERS_NUM];

  printf(1, "malloc_test_%d...\n", num);
  printf(1, "      initiating...\n");
  for (int i = 0; i < BUFFERS_NUM; i++) {
    printf(1, ".");
    buffers[i] = malloc(BUFFERS_SIZE);
    buffers[i][0] = i;
  }

  printf(1, " \n      testing...\n");
  for (int i = 0; i < BUFFERS_NUM; i++) {
    printf(1, ".");
    if (buffers[i][0] != i) {
      printf(1, "malloc_test failed...");
      exit();
    }
  }  

  printf(1, " \n      freeing...\n");
  for (int i = 0; i < BUFFERS_NUM; i++) {
    printf(1, ".");
    free(buffers[i]);
  } 

  printf(1, "\n  malloc_test_%d ok...\n", num);  
}

void
fork_test(int num)
{
  int* buffers[BUFFERS_NUM];

  printf(1, "fork_test_%d...\n", num);
  printf(1, "      initiating...\n");
  for (int i = 0; i < BUFFERS_NUM; i++) {
    printf(1, ".");
    buffers[i] = malloc(BUFFERS_SIZE);
    buffers[i][0] = i;
  }

  printf(1, " \n      testing father...\n");
  for (int i = 0; i < BUFFERS_NUM; i++) {
    printf(1, ".");
      if (buffers[i][0] != i) {
        printf(1, "fork_test failed (%d != %d)...", buffers[i][0], i);
        exit();
      }
  }

  printf(1, " \n      testing father 2...\n");
  for (int i = 0; i < BUFFERS_NUM; i++) {
    printf(1, ".");
      if (buffers[i][0] != i) {
        printf(1, "fork_test failed (%d != %d)...", buffers[i][0], i);
        exit();
      }
  }  

  if (fork() == 0) {
    printf(1, " \n      testing...\n");
    for (int i = 0; i < BUFFERS_NUM; i++) {
      printf(1, ".");
        if (buffers[i][0] != i) {
          printf(1, "fork_test failed (%d != %d)...\n", buffers[i][0], i);   
          exit();     
        }
    }

    printf(1, "\n  fork_test_%d ok...\n", num);
    exit();
  } 

  for (int i = 0; i < BUFFERS_NUM; i++) {
    free(buffers[i]);
  }   

  wait();
}

void
exec_test()
{
  printf(1, "exec_test...\n");

  if(fork() == 0) {
    char* args[] = {"myMemTest", "1", 0};
    exec(args[0], args);
  }

  wait();

  printf(1, "exec_test ok...\n");
}

void
policy_test() 
{
  int* buffers[POLICY_BUFFERS_NUM];
  int order[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8,
                 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
  
  printf(1, "policy_test...\n");
  printf(1, "      initiating...\n");
  for (int i = 0; i < POLICY_BUFFERS_NUM; i++) {
    printf(1, ".");
    buffers[i] = malloc(BUFFERS_SIZE * 4);
    for (int j = 0; j < BUFFERS_SIZE; j++) {
      buffers[i][j] = i * j;
    }
    
  }

  int up = uptime();
  printf(1, " \n      testing...\n");
  for (int k = 0; k < 20; k++) {
    for (int i = 0; i < sizeof(order) / 4; i++) {
      printf(1, ".");
      int index = order[i];
      for (int j = 0; j < 50; j++) {
        if (buffers[index][j] != index * j) {
          printf(1, "policy_test failed...");
          exit();
        }
      }
    } 
  }
  up = uptime() - up;
  
  printf(1, " \n      freeing...\n");
  for (int i = 0; i < POLICY_BUFFERS_NUM; i++) {
    printf(1, ".");
    free(buffers[i]);
  } 


  
  printf(1, "\n  policy_test ok(%d ticks)...\n", up);  
}

int
main(int argc, char *argv[])
{
    if (argc == 2) {
      printf(1, "(exec)(exec)(exec)...\n");
      fork_test(1);
      malloc_test(1);
    } else if (argc == 3) {
      printf(1, "(exec)(exec)(exec)...\n");
      malloc_test(1);
      exec_test();
    } else if (argc == 4) {
      printf(1, "myMemTest starting...\n");
      fork_test(1);
      malloc_test(1);
      malloc_test(2);
      malloc_test(3);
      fork_test(2);
      malloc_test(4);
    } else {
      policy_test() ;
    }

    exit();
}
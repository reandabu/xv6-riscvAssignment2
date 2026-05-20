//--------------------------------Task1
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NPROC 15
#define GROUPS 4

int
main(void)
{
  int lock_id = israeli_create(50);

  if(lock_id < 0){
    printf("failed to create Israeli lock\n");
    exit(1);
  }

  lcg_srand(getpid());

  for(int i = 0; i < NPROC; i++){
    int pid = fork();

    if(pid == 0){
      int gid = lcg_rand() % GROUPS;
      setgid(gid);

      if(israeli_acquire(lock_id) < 0){
        printf("Process %d failed to acquire lock\n", getpid());
        exit(1);
      }

      printf("Process %d (gid=%d) acquired the lock\n", getpid(), getgid());

      sleep(10);

      if(israeli_release(lock_id) < 0){
        printf("Process %d failed to release lock\n", getpid());
        exit(1);
      }

      exit(0);
    }
  }

  for(int i = 0; i < NPROC; i++)
    wait(0);

  if(israeli_destroy(lock_id) < 0)
    printf("failed to destroy Israeli lock\n");

  exit(0);
}
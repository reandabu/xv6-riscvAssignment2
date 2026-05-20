//--------------------------Task0
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  lcg_srand(1);

  printf("rand 1 = %d\n", lcg_rand());
  printf("rand 2 = %d\n", lcg_rand());
  printf("rand 3 = %d\n", lcg_rand());

  exit(0);
}
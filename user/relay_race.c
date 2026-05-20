//---------------------- Task3
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define TEAMS 3
#define RUNNERS_PER_TEAM 5
#define TARGET_SCORE 30

void
run_race(int favoritism)
{
  int lock_id;
  int total_runners = TEAMS * RUNNERS_PER_TEAM;

  printf("\n=== Relay race: favoritism = %d ===\n", favoritism);

  if(relay_reset(TEAMS) < 0){
    printf("relay_reset failed\n");
    exit(1);
  }

  lock_id = israeli_create(favoritism);
  if(lock_id < 0){
    printf("israeli_create failed\n");
    exit(1);
  }

  for(int i = 0; i < total_runners; i++){
    int pid = fork();

    if(pid < 0){
      printf("fork failed\n");
      exit(1);
    }

    if(pid == 0){
      int team = i % TEAMS;
      setgid(team);

      for(;;){
        if(relay_winner(TARGET_SCORE) >= 0)
          exit(0);

        if(israeli_acquire(lock_id) < 0)
          exit(1);

        if(relay_winner(TARGET_SCORE) >= 0){
          israeli_release(lock_id);
          exit(0);
        }

        int score = relay_inc(team);

        printf("Runner %d (Team %d) acquired the baton\n", getpid(), team);
        printf("Team %d score = %d\n", team, score);

        israeli_release(lock_id);

        if(score >= TARGET_SCORE)
          exit(0);

        sleep(2);
      }
    }
  }

  for(int i = 0; i < total_runners; i++)
    wait(0);

  int winner = relay_winner(TARGET_SCORE);

  printf("Final scores for favoritism %d:\n", favoritism);
  for(int t = 0; t < TEAMS; t++){
    printf("Team %d final score = %d\n", t, relay_get(t));
  }

  printf("Winner = Team %d\n", winner);

  if(israeli_destroy(lock_id) < 0)
    printf("israeli_destroy failed\n");
}

int
main(void)
{
  run_race(0);
  run_race(50);
  run_race(100);

  exit(0);
}
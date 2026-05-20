#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
// ---------------- Task 0: LCG pseudo-random generator ----------------

static struct spinlock lcg_lock;
static int lcg_initialized = 0;
static uint lcg_state = 1;

static void
lcg_initlock(void)
{
  if(lcg_initialized == 0){
    initlock(&lcg_lock, "lcg");
    lcg_initialized = 1;
  }
}

void
lcg_srand(uint seed)
{
  lcg_initlock();

  acquire(&lcg_lock);
  lcg_state = seed;
  release(&lcg_lock);
}

uint
lcg_rand(void)
{
  uint result;

  lcg_initlock();

  acquire(&lcg_lock);
  lcg_state = lcg_state * 1664525 + 1013904223;
  result = lcg_state;
  release(&lcg_lock);

  return result;
}

uint64
sys_lcg_srand(void)
{
  int seed;

  argint(0, &seed);
  lcg_srand((uint)seed);
  return 0;
}

uint64
sys_lcg_rand(void)
{
  return lcg_rand();
}
// ---------------- Task 0: LCG pseudo-random generator ----------------

// ---------------- Task 1: Process gid ----------------

uint64
sys_setgid(void)
{
  int gid;

  argint(0, &gid);
  myproc()->gid = gid;
  return 0;
}

uint64
sys_getgid(void)
{
  return myproc()->gid;
}


// ---------------- Task 1: Israeli Lock ----------------

#define NISRAELI 15
#define ISRAELI_MAX_WAITERS 16

struct israeli_lock {
  struct spinlock lock;
  int active;
  int locked;
  int favoritism;
  struct proc *owner;

  struct proc *queue[ISRAELI_MAX_WAITERS];
  int qsize;
};

static struct israeli_lock israeli_locks[NISRAELI];

void
israeli_init(void)
{
  for(int i = 0; i < NISRAELI; i++){
    initlock(&israeli_locks[i].lock, "israeli");
    israeli_locks[i].active = 0;
    israeli_locks[i].locked = 0;
    israeli_locks[i].favoritism = 0;
    israeli_locks[i].owner = 0;
    israeli_locks[i].qsize = 0;
  }
}

static int
valid_lock_id(int lock_id)
{
  return lock_id >= 0 && lock_id < NISRAELI;
}

int
israeli_create(int favoritism)
{
  if(favoritism < 0 || favoritism > 100)
    return -1;

  for(int i = 0; i < NISRAELI; i++){
    acquire(&israeli_locks[i].lock);

    if(israeli_locks[i].active == 0){
      israeli_locks[i].active = 1;
      israeli_locks[i].locked = 0;
      israeli_locks[i].favoritism = favoritism;
      israeli_locks[i].owner = 0;
      israeli_locks[i].qsize = 0;

      release(&israeli_locks[i].lock);
      return i;
    }

    release(&israeli_locks[i].lock);
  }

  return -1;
}

static int
is_in_queue(struct israeli_lock *lk, struct proc *p)
{
  for(int i = 0; i < lk->qsize; i++){
    if(lk->queue[i] == p)
      return 1;
  }
  return 0;
}

static int
enqueue_proc(struct israeli_lock *lk, struct proc *p)
{
  if(lk->qsize >= ISRAELI_MAX_WAITERS)
    return -1;

  if(is_in_queue(lk, p))
    return 0;

  lk->queue[lk->qsize++] = p;
  return 0;
}

static struct proc*
remove_from_queue(struct israeli_lock *lk, int idx)
{
  struct proc *p = lk->queue[idx];

  for(int i = idx; i < lk->qsize - 1; i++)
    lk->queue[i] = lk->queue[i + 1];

  lk->qsize--;
  return p;
}

static int
choose_next_index(struct israeli_lock *lk, int releasing_gid)
{
  int fav_idx = -1;

  for(int i = 0; i < lk->qsize; i++){
    if(lk->queue[i]->gid == releasing_gid){
      fav_idx = i;
      break;
    }
  }

  if(fav_idx != -1){
    uint r = lcg_rand() % 100;
    if(r < (uint)lk->favoritism)
      return fav_idx;
  }

  return 0; // FIFO
}

int
israeli_acquire(int lock_id)
{
  struct proc *p = myproc();

  if(!valid_lock_id(lock_id))
    return -1;

  struct israeli_lock *lk = &israeli_locks[lock_id];

  acquire(&lk->lock);

  if(lk->active == 0){
    release(&lk->lock);
    return -1;
  }

  if(lk->locked == 0 && lk->qsize == 0){
    lk->locked = 1;
    lk->owner = p;
    release(&lk->lock);
    return 0;
  }

  if(enqueue_proc(lk, p) < 0){
    release(&lk->lock);
    return -1;
  }

  while(lk->active && lk->owner != p){
    sleep(p, &lk->lock);
  }

  if(lk->active == 0){
    release(&lk->lock);
    return -1;
  }

  release(&lk->lock);
  return 0;
}

int
israeli_release(int lock_id)
{
  struct proc *p = myproc();

  if(!valid_lock_id(lock_id))
    return -1;

  struct israeli_lock *lk = &israeli_locks[lock_id];

  acquire(&lk->lock);

  if(lk->active == 0 || lk->locked == 0 || lk->owner != p){
    release(&lk->lock);
    return -1;
  }

  if(lk->qsize == 0){
    lk->locked = 0;
    lk->owner = 0;
  } else {
    int idx = choose_next_index(lk, p->gid);
    struct proc *next = remove_from_queue(lk, idx);

    lk->owner = next;
    lk->locked = 1;
    wakeup(next);
  }

  release(&lk->lock);
  return 0;
}

int
israeli_destroy(int lock_id)
{
  if(!valid_lock_id(lock_id))
    return -1;

  struct israeli_lock *lk = &israeli_locks[lock_id];

  acquire(&lk->lock);

  if(lk->active == 0 || lk->locked || lk->qsize > 0){
    release(&lk->lock);
    return -1;
  }

  lk->active = 0;
  lk->locked = 0;
  lk->favoritism = 0;
  lk->owner = 0;
  lk->qsize = 0;

  release(&lk->lock);
  return 0;
}

uint64
sys_israeli_create(void)
{
  int favoritism;

  argint(0, &favoritism);
  return israeli_create(favoritism);
}

uint64
sys_israeli_acquire(void)
{
  int lock_id;

  argint(0, &lock_id);
  return israeli_acquire(lock_id);
}

uint64
sys_israeli_release(void)
{
  int lock_id;

  argint(0, &lock_id);
  return israeli_release(lock_id);
}

uint64
sys_israeli_destroy(void)
{
  int lock_id;

  argint(0, &lock_id);
  return israeli_destroy(lock_id);
}
// ---------------- Task 1: Israeli Lock ----------------

// ---------------- Task 2: Relay race score table ----------------

#define RELAY_MAX_TEAMS 16

static struct spinlock relay_lock;
static int relay_initialized = 0;
static int relay_teams = 0;
static int relay_scores[RELAY_MAX_TEAMS];

static void
relay_initlock(void)
{
  if(relay_initialized == 0){
    initlock(&relay_lock, "relay");
    relay_initialized = 1;
  }
}

static int
relay_reset(int teams)
{
  relay_initlock();

  if(teams <= 0 || teams > RELAY_MAX_TEAMS)
    return -1;

  acquire(&relay_lock);

  relay_teams = teams;
  for(int i = 0; i < RELAY_MAX_TEAMS; i++)
    relay_scores[i] = 0;

  release(&relay_lock);
  return 0;
}

static int
relay_inc(int team)
{
  int score;

  relay_initlock();

  acquire(&relay_lock);

  if(team < 0 || team >= relay_teams){
    release(&relay_lock);
    return -1;
  }

  relay_scores[team]++;
  score = relay_scores[team];

  release(&relay_lock);
  return score;
}

static int
relay_get(int team)
{
  int score;

  relay_initlock();

  acquire(&relay_lock);

  if(team < 0 || team >= relay_teams){
    release(&relay_lock);
    return -1;
  }

  score = relay_scores[team];

  release(&relay_lock);
  return score;
}

static int
relay_winner(int target)
{
  int winner = -1;

  relay_initlock();

  acquire(&relay_lock);

  for(int i = 0; i < relay_teams; i++){
    if(relay_scores[i] >= target){
      winner = i;
      break;
    }
  }

  release(&relay_lock);
  return winner;
}

uint64
sys_relay_reset(void)
{
  int teams;

  argint(0, &teams);
  return relay_reset(teams);
}

uint64
sys_relay_inc(void)
{
  int team;

  argint(0, &team);
  return relay_inc(team);
}

uint64
sys_relay_get(void)
{
  int team;

  argint(0, &team);
  return relay_get(team);
}

uint64
sys_relay_winner(void)
{
  int target;

  argint(0, &target);
  return relay_winner(target);
}
// ---------------- Task 2: Relay race score table ----------------

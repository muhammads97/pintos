#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/fixedpt.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
struct list ready_array[PRI_MAX + 1];
int64_t load_avg;
/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
#define TIME_FREQ 100
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
struct semaphore file_access;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
static void init_advanced_ready_queue();
static void calculate_priority(struct thread *t);
static void recalculate_all_priorities();
static void calculate_recent_cbu(struct thread *t);
static void calculate_all_recent_cbu();

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  if(thread_mlfqs){
    init_advanced_ready_queue();
  } else {
    list_init (&ready_list);
  }
  
  list_init (&all_list);
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  load_avg = 0;
  initial_thread->recent_cpu = 0;
  initial_thread->nice = NICE_DEFAULT;

  sema_init(&file_access, 1);
  
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if(thread_mlfqs){
    thread_ticks++;
    
    if(t!= idle_thread && t->status == THREAD_RUNNING) t->recent_cpu += toFixedPt(1);

    if(timer_ticks() % TIME_FREQ == 0){
      int r_threads = 0;
      for(int i = 0; i <= PRI_MAX; i++){
        r_threads += list_size(&ready_array[i]);
      }
      if(t != idle_thread) r_threads++;
      load_avg = (multiplyXY(load_avg, toFixedPt(59)) + toFixedPt(1) * r_threads) / 60;
      // printf("update load avg %d %d\n", load_avg, r_threads);
      calculate_all_recent_cbu();
    }
    if(thread_ticks % TIME_SLICE == 0){
      recalculate_all_priorities();
      if(t != initial_thread){
        intr_yield_on_return();
      }
    }
    
    
  } else{
    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
      intr_yield_on_return ();
  }
}


/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);
  if(free_pages_cnt() < 7) return TID_ERROR;
  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;
  // if(thread_current()->depth + 1 > 31) return TID_ERROR;
  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;
  /* Add to run queue. */
  t->parent = thread_current();
  t->ch_tid = -1;
  t->descriptor = 1;
  t->exec_child_success = false;
  t->child_exit_status = -1;
  list_push_back(&thread_current()->children, &t->to_be_child);
  // printf("kkkkkkkkkk %d %s\n", list_size(&thread_current()->children), thread_name());
  thread_unblock (t);

  if(t->tid != 1 && t->priority > thread_current()->priority){
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
  ASSERT (is_thread (t));
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if(thread_mlfqs){
    list_push_back(&ready_array[t->priority], &t->elem);
    // printf("%d  %d\n", list_size(&ready_array[t->priority]), t->priority);
  } else {
    list_insert_ordered (&ready_list, &t->elem, priority_comparator, NULL);
  }
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) {
    if(thread_mlfqs){
      list_push_back(&ready_array[cur->priority], &cur->elem);
    } else {
      list_insert_ordered (&ready_list, &cur->elem, priority_comparator, NULL);
    }
  } 
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}
void yield_if_a_ready_is_higher(){  
  if(thread_current()->tid == 2) return;
  if(thread_mlfqs){
    int cur_priority = thread_current()->priority;
    int i = PRI_MAX;
    bool y = false;
    while(i >= 0){
      if(cur_priority >= i) break;
      if(!list_empty(&ready_array[i])) {
        y = true;
        break;
      }
      i++;
    }
    if(y) thread_yield();
  } else {
    struct thread *temp = list_entry(list_front(&ready_list), struct thread, elem);
    if(temp->priority > thread_current()->priority){
      thread_yield();
    }
  }
}
/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  enum intr_level old_level = intr_disable();
  if(list_empty(&thread_current()->donations)){
    thread_current ()->priority = new_priority;
    thread_current ()->basepriority = new_priority;
  } else {
    struct list_elem * e = list_front(&thread_current()->donations);
    struct thread * highest_donation_thread = list_entry(e, struct thread, donate_elem);
    if(highest_donation_thread->priority > new_priority){
      thread_current ()->basepriority = new_priority;
    } else {
      thread_current ()->priority = new_priority;
      thread_current ()->basepriority = new_priority;
    }
  }
  struct thread * temp = thread_current();
  while(temp->locker){
    if(temp->priority > temp->locker->priority){
      temp->locker->priority = temp->priority;
      temp = temp->locker;
    }
  }
  //sort ready list
  list_sort(&ready_list, priority_comparator, NULL);
  //yield or not
  if(!list_empty(&ready_list)){
    temp = list_entry(list_front(&ready_list), struct thread, elem);
    if(temp->priority > thread_current()->priority){
      thread_yield();
    }
  }
  
  intr_set_level(old_level);
}
void sort_ready_list(){
  list_sort(&ready_list, priority_comparator, NULL);
}
/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  thread_current()->nice = nice;
  calculate_priority(thread_current());
  yield_if_a_ready_is_higher();
}
static void calculate_priority(struct thread *t){
  t->priority = PRI_MAX - toInteger(t->recent_cpu * 6 / 20) - t->nice * 2;
  if(t->priority > PRI_MAX) t->priority = PRI_MAX;
  else if(t->priority < PRI_MIN) t->priority = PRI_MIN;
}
static void recalculate_all_priorities(){
  enum intr_level old_level = intr_disable();
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      calculate_priority(t);
    }
  intr_set_level(old_level);
}
/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return toIntegerRounded(load_avg * 100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return toIntegerRounded(thread_current()->recent_cpu * 100);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->basepriority = priority;
  t->magic = THREAD_MAGIC;
  t->waiting_for = NULL;
  t->locker = NULL;
  list_init(&t->donations);
  list_init(&t->children);
  list_init(&t->files);
  sema_init(&t->wait_for_child, 0);
  sema_init(&t->exec, 0);
  if(thread_mlfqs){
    calculate_priority(t);
    if(t != initial_thread){
      t->nice = thread_current()->nice;
      t->recent_cpu = thread_current()->recent_cpu;
    }
  }

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);
  t->stack -= size;
  // if(is_user_vaddr(t->stack)){
  //   return NULL;
  // }
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if(thread_mlfqs){
    int i = PRI_MAX;
    while (i >= 0){
      if(!list_empty(&ready_array[i])){
        return list_entry(list_pop_front(&ready_array[i]), struct thread, elem);
      }
      i--;
    }
    return idle_thread;
  } else {
    if (list_empty (&ready_list))
      return idle_thread;
    else
      return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
  
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

bool priority_comparator(struct list_elem *a, struct list_elem *b, void *aux){
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);
  return t1-> priority > t2->priority;
}

// bool advanced_queue_comparator(struct list_elem* a, struct list_elem*b, void * aux){
//   struct advanced_ready_queue * q1 = list_entry(a, struct advanced_ready_queue, elem);
//   struct advanced_ready_queue * q2 = list_entry(b, struct advanced_ready_queue, elem);
//   if(q1->priority > q2->priority){
//     if(!list_empty(&q1->list)) return true;
//     if(!list_empty(&q2->list)) return false;
//     return true;
//   } else {
//     if(!list_empty(&q2->list)) return false;
//     if(!list_empty(&q1->list)) return true;
//     return false;
//   }
// }

static void init_advanced_ready_queue(){
  for(int i = 0; i <= PRI_MAX; i++){
    list_init(&ready_array[i]);
  }
}

static void calculate_recent_cbu(struct thread *t){
  t->recent_cpu = toFixedPt(t->nice) + multiplyXY(t->recent_cpu, divideXY(load_avg * 2, (load_avg * 2) + toFixedPt(1)));
}
static void calculate_all_recent_cbu(){
  enum intr_level old_level = intr_disable();
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      calculate_recent_cbu(t);
    }
  intr_set_level(old_level);
}

void print_list_ready(){
  struct list_elem *e = list_front(&ready_list);
  int i = 0;
  while(e != list_end(&ready_list)){
    struct thread *t = list_entry(e, struct thread, elem);
    printf("%d thread %d with priority %d \n", i, t->tid, t->priority);
    i++;
    e = list_next(e);
  }
}
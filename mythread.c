#include "mythread.h"
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>

#define THREADSTACK 8192

typedef struct node {
 void *object; 
 struct node * next;
} Node;

typedef struct queue {
  Node * front;
  Node * rear;
  int count;
} Queue;

typedef struct MyThreadStruct {
  int tid;
  int child_tid_join; 
  int join_all;
  ucontext_t context;
  Queue child_queue;
  struct MyThreadStruct *parent;
} Thread;

typedef struct MySemaphoreStruct {
  int value;
  Queue semephore_queue;
} Semaphore;


void * dequeue_object(Queue *);

int global_tid = 0; //-1= no thread; 0= Unix thread; >0= child threads
void * queue_object_value; // For refering objects to queue functions
Thread * running_thread; // Master thread which is in running state
ucontext_t unix_context; // Context of the Unix process set during init

Queue ready_queue = {NULL, NULL,0};
Queue blocked_queue = {NULL, NULL,0};

void equeue_object(Queue * queue_value) {

  Node * temp = (Node *) calloc(1,sizeof(Node));
  queue_value->count++;
  temp->object = queue_object_value;

  if (queue_value->front == NULL) {

    queue_value->front = temp;
    queue_value->rear = temp;
    return;
  }
  queue_value->rear->next = temp;
  queue_value->rear = temp;
}

void * dequeue_object(Queue *queue_value) {

  if (queue_value->count == 0) {
    return NULL;
  }

  void * object = queue_value->front->object;
  
  if (queue_value->count == 1) {
    free(queue_value->front);
    queue_value->rear = NULL;
    queue_value->front = NULL;

  } else {
    Node * temp = queue_value->front;
    queue_value->front = queue_value->front->next;
    free(temp);
  }
  queue_value->count--;
  return object;
}
void remove_node(Queue * queue_value) {
  if (queue_value->count == 0) {
    return;
  }
  if (queue_value->front->object == queue_object_value) {
    (void)dequeue_object(queue_value);
    return;
  }
  Node * temp_1 =  queue_value->front;
  Node * temp_2 =  queue_value->front->next;

  while (temp_2 != NULL) {
    if (temp_2->object == queue_object_value) {

      if (queue_value->rear == temp_2) {
        queue_value->rear = temp_1;
      }
      queue_value->count--;
      temp_1->next = temp_2->next;
      free(temp_2);
      return;
    }
    temp_1 = temp_2;
    temp_2 = temp_2->next;
  }
}

int search_queue(Queue *queue_value) {
  Node * temp = queue_value->front;
  while (temp != NULL) {
    if (temp->object == queue_object_value) {
      return 1;
    }
    temp = temp->next;
  }
  return 0;
}



MyThread MyThreadCreate(void(*start_funct)(void *), void *args) {

  Thread * child = (Thread *) calloc(1, sizeof(Thread));
  if (child == NULL) { 
    printf("THREADCREATE: No Memory\n"); 
    return NULL;
  }
  ucontext_t *temp_context = &child->context;  
  getcontext(temp_context);

  temp_context->uc_link = &running_thread->context;
  temp_context->uc_stack.ss_sp= malloc(THREADSTACK);
  temp_context->uc_stack.ss_size=THREADSTACK;
  temp_context->uc_stack.ss_flags=0;
  makecontext(temp_context, (void (*)(void))start_funct, 1, args);
  
  child->tid = global_tid;
  global_tid++;
  
  child->parent = running_thread;
  queue_object_value = child;
  // Put newly created thread in ready queue
  equeue_object(&ready_queue);
  // Make this newly created thread a child of thread which is currently running
  equeue_object(&running_thread->child_queue);

  return (MyThread)child;  
}

void MyThreadYield(void) {

  // Yield running thread by enqueuing it in ready queue
  queue_object_value = running_thread;
  equeue_object(&ready_queue);

  // putting thread from ready queue in to running state
  if (ready_queue.count > 0) {
    Thread * temp_running = running_thread;
    running_thread = (Thread *)dequeue_object(&ready_queue);
    swapcontext(&temp_running->context, &running_thread->context);
  }
  else{
    setcontext(&unix_context);
  }
}

int MyThreadJoin(MyThread thread) {
  
  Thread * child = (Thread *) thread;
  queue_object_value = child;
  int ret = search_queue(&running_thread->child_queue);
  // If thread in parameter is running thread's child then
  if (ret == 1) { 
    // Put child thread id in parents datastructure for record to block on it during exit
    running_thread->child_tid_join = child->tid;
    // Enqueue to blocked queue
    queue_object_value = running_thread;
    equeue_object(&blocked_queue);

    // putting thread from ready queue in to running state
    if (ready_queue.count > 0) {
    Thread * temp_running = running_thread;
    running_thread = (Thread *)dequeue_object(&ready_queue);
    swapcontext(&temp_running->context, &running_thread->context);
    } else {
      setcontext(&unix_context);
    }
  } else {
    return -1;
  }
  return 0;
}


void MyThreadJoinAll(void) {
  
  // Thread has to have child to set join all
  if (running_thread->child_queue.count > 0) {
    // Set join all flag
    running_thread->join_all = 1;
    
    // Enqueue to blocked queue
    queue_object_value = running_thread;
    equeue_object(&blocked_queue);

    // putting thread from ready queue in to running state
    if (ready_queue.count > 0) {
      Thread * temp_running = running_thread;
      running_thread = (Thread *)dequeue_object(&ready_queue);
      swapcontext(&temp_running->context, &running_thread->context);
    } else {
      setcontext(&unix_context);
    }
  }
}

// Start: Help sort from following for MyThreadExit
// https://github.com/aneesh87/User-Level-Thread-Library
void MyThreadExit(void) {

  Thread * rths_parent = running_thread->parent;
  // Check whether running threads parent is live?
  if (rths_parent != NULL) {
    queue_object_value = running_thread;
    remove_node(&rths_parent->child_queue);
    queue_object_value = rths_parent;

    // Look for running threads parent in blocked queue, if it exists
    if (search_queue(&blocked_queue)) {
      // Check whether join all flag is set for this parent
      if (rths_parent->join_all){ 
        // if join all set, check whether all children have exited
        if(rths_parent->child_queue.count == 0) {
          queue_object_value = running_thread;
          remove_node(&blocked_queue);
          queue_object_value = rths_parent;
          equeue_object(&ready_queue);
          rths_parent->join_all = 0;
        }
      } // Otherwise, check for simple join based on thread id match 
      else {
        if (rths_parent->child_tid_join == running_thread->tid) {
            queue_object_value = running_thread;            
            remove_node(&blocked_queue);
            queue_object_value = rths_parent;
            equeue_object(&ready_queue);
            // -1 thread id indicates that no thread is attached
            rths_parent->child_tid_join = -1;
        }  
      }
    }
  }
  if (ready_queue.count > 0) {
    running_thread = (Thread *)dequeue_object(&ready_queue);;
    setcontext(&running_thread->context);
  } else {
    setcontext(&unix_context);        
  } 
}
// End

MySemaphore MySemaphoreInit(int initialValue) {
  if (initialValue < 0) { 
    printf("SEMERROR: Initial Value less than zero\n");
    return NULL;
  }

  Semaphore * temp_sem = (Semaphore *)calloc(1, sizeof(Semaphore));
  temp_sem->value = initialValue;
  return (MySemaphore) temp_sem;
}

void MySemaphoreSignal(MySemaphore sem) {
  if (sem == NULL) { 
    printf("SEMERROR: Signal Sem NULL\n");
    return;
  }

  Semaphore * temp_sem = (Semaphore *)sem;
  temp_sem->value++;
  if (temp_sem->value <= 0) {
    Thread * temp_thread = (Thread *)dequeue_object(&temp_sem->semephore_queue);
    queue_object_value = temp_thread;
    equeue_object(&ready_queue);
  }
}

void MySemaphoreWait(MySemaphore sem) {
  if (sem == NULL) { 
    printf("SEMERROR: Wait Sem NULL\n");
    return;
  }
  Semaphore * temp_sem = (Semaphore *)sem;
  temp_sem->value--;

  if (temp_sem->value < 0) {
    queue_object_value = running_thread;
    equeue_object(&temp_sem->semephore_queue);
    // putting thread from ready queue in to running state
    if (ready_queue.count > 0) {
      Thread * temp_running = running_thread;
      running_thread = (Thread *)dequeue_object(&ready_queue);
      swapcontext(&temp_running->context, &running_thread->context);
    }else{
      setcontext(&unix_context);
    }
  }
}

int MySemaphoreDestroy(MySemaphore sem) {
  if (sem == NULL) { 
    printf("SEMERROR: Destroy Sem NULL\n");
    return -1;
  }
  Semaphore * temp_sem = (Semaphore *)sem;
  if (temp_sem->semephore_queue.count > 0) { 
    printf("SEMQUEUE: Not Empty\n");
    return -1;
  }
  free(temp_sem);
  return 0;
}


void MyThreadInit(void(*start_funct)(void *), void *args) {

  ucontext_t *temp_context;
  running_thread = (Thread *) calloc(1, sizeof(Thread));
  // Set thread id
  running_thread->tid = global_tid;
  global_tid++;
  temp_context = &running_thread->context;
  getcontext(&unix_context);  
  getcontext(temp_context);
  // Assigning parameters to temp_conext for runnung_thread
  temp_context->uc_link = NULL;
  temp_context->uc_stack.ss_sp=malloc(THREADSTACK);
  temp_context->uc_stack.ss_size=THREADSTACK;
  temp_context->uc_stack.ss_flags=0;

  makecontext(temp_context, (void (*)(void))start_funct, 1, args);
  // Activating context
  swapcontext(&unix_context, temp_context);
}

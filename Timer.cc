#include "Timer.h"

#include "CommonTask.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <vector>

#include <assert.h>

#define TRUE  0x00000001
#define FALSE 0x00000000

#ifdef DEBUG
  #define yoseob(fmt, args...) fprintf(stderr, "[%s:%d:%s()]: " fmt, \
    __FILE__, __LINE__, __func__, ##args)
#else
  #define yoseob(fmt, args...)
#endif

enum TimerState
{
  UNKNOWN = 0,
  START,
  STOP
};

#ifdef __linux__
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

const int kMaxEventSize = 10;

typedef struct _timer_data_t
{
  int timer_fd = 0;
  CommonTask* task = nullptr;
} timer_data_t;

typedef struct TimerImpl_
{
  int epoll_fd = 0;
  std::vector<timer_data_t> datas;
  pthread_t main_thread_id;
  epoll_event events[kMaxEventSize];
  uint32_t stop = FALSE;
  TimerState state = UNKNOWN;
} TimerImpl;

#else
#include <time.h>
#include <sys/time.h>

// for other unix systems like mac, freebsd and so on...
typedef struct _timer_data_t
{
  int period = 0;
  CommonTask* task = nullptr;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
} timer_data_t;

uint32_t stop = FALSE;

typedef struct TimerImpl_
{
  std::vector<pthread_t> thread_ids;
  std::vector<timer_data_t *> datas;
  TimerState state = UNKNOWN;
} TimerImpl;

#endif

// thread function
void* timer_thread_func(void *pData)
{
#ifdef __linux__
  TimerImpl* this_ = (TimerImpl*)pData;
  int num_of_fds = 0;
  // using epoll. work as a control thread
  while(!this_->stop)
  {
    // wait timer events.
    num_of_fds = epoll_wait(this_->epoll_fd, this_->events, kMaxEventSize, -1);
    if(num_of_fds < 0)
    {
      perror("epoll_wait");
      assert(0);
      return (void *)EXIT_FAILURE;
    }

    for(int i = 0; i < num_of_fds; i++)
    {
      for(int j = 0; j < this_->datas.size(); j++) 
      {
        if(this_->events[i].data.fd == this_->datas[j].timer_fd) {
          // timer is triggered
          uint64_t dummy = 0;
          int size = read(this_->datas[j].timer_fd, &dummy, sizeof(dummy));
          if(size != -1) 
            this_->datas[j].task->execute();
          else
            perror("read");
        }
      } 
    }
  }
  
  return (void *)0;
#else
  // thread per task. work as task thread
  timer_data_t* pData_ = (timer_data_t *)pData;
  pthread_mutex_lock(&pData_->mutex);
  pthread_cond_signal(&pData_->cond);
  pthread_mutex_unlock(&pData_->mutex);

  while(!stop)
  {
    pData_->task->execute();
    struct timeval now;
    struct timespec ts;
    gettimeofday(&now, NULL);
    ts.tv_sec = now.tv_sec + pData_->period;
    pthread_mutex_lock(&pData_->mutex);
    pthread_cond_timedwait(&pData_->cond, &pData_->mutex, &ts); // use condition variable to sync
    pthread_mutex_unlock(&pData_->mutex);
//    sleep(pData_->period); // use sleep to waiting
  }

  delete pData_; // delete timer_data_t

  return (void *)0;
#endif
}

Timer::Timer()
{
  pImpl = (void *)new TimerImpl_();
  memset(pImpl, 0, sizeof(TimerImpl_));
}

Timer::~Timer()
{
  TimerImpl* this_ = (TimerImpl*)pImpl;

  if(this_->state != TimerState::STOP)
    stoptimer();

  delete this_;
}

bool Timer::starttimer() 
{
  TimerImpl* this_ = (TimerImpl *)pImpl;
  if(this_->state == TimerState::START) return true;
  this_->state = TimerState::START;
#ifdef __linux__
  // create epoll
  this_->epoll_fd = epoll_create(kMaxEventSize);
  if(this_->epoll_fd == -1)
  {
    perror("epoll_create");
    assert(0);
    return false;
  }
  // make control thread
  if(pthread_create(&this_->main_thread_id, NULL, timer_thread_func, (void*)this_) < 0)
  {
    perror("pthread_create");
    assert(0);
    return false;
  }
  yoseob("main thread %lu running!!\n", (unsigned long)this_->main_thread_id);
  return true;
#else
  // DO NOTHING.
  return true; 
#endif
}

bool Timer::stoptimer()
{
  yoseob("stoptimer\n");
  TimerImpl* this_ = (TimerImpl *)pImpl;
  if(this_->state != START) return true;
  this_->state = TimerState::STOP;
  int status = 0;
#ifdef __linux__
  this_->stop = TRUE;
  int efd = eventfd(0, 0); // event fd for wake up epoll_wait
  if(efd == -1)
  {
    perror("eventfd"); 
    assert(0);
    return false;
  }
  epoll_event event;
  event.data.fd = efd;
  event.events = EPOLLIN | EPOLLET;
  if(epoll_ctl(this_->epoll_fd, EPOLL_CTL_ADD, efd, &event) == -1)
  {
    perror("epoll_ctl: event_fd");
    assert(0);
    return false;
  }

  eventfd_write(efd, 1); // wake up epoll_wait
 
  for(int i = 0; i < this_->datas.size(); i++)
  {
    epoll_event dummy; 
    // dummy event.
    //       In kernel versions before 2.6.9, the EPOLL_CTL_DEL operation required
    //       a non-null pointer in event, even though this argument is ignored.
    if(epoll_ctl(this_->epoll_fd, EPOLL_CTL_DEL, this_->datas[i].timer_fd, &dummy) == -1)
    {
      perror("epoll_ctl: timer_fd");
      assert(0);
      return false;
    }
    close(this_->datas[i].timer_fd);
  }
  // Stop main control thread
  yoseob("main thread(%lu) stopping...\n", (unsigned long)this_->main_thread_id);
  pthread_join(this_->main_thread_id, (void **)&status);
  yoseob("main thread end. status : %d\n", status);
  
  close(this_->epoll_fd); // close epoll fd
  this_->epoll_fd = 0;
  close(efd); // close temporary event fd

  this_->datas.clear(); // clear all datas

  return true;
#else
  stop = true;
  // Just stop all threads.
  for(int i = 0; i < this_->thread_ids.size(); i++)
  {
    pthread_cond_signal(&this_->datas[i]->cond); // wakeup
    yoseob("task thread[%d](%lu) stopping...\n", i, (unsigned long)this_->thread_ids[i]);
    pthread_join(this_->thread_ids[i], (void **)&status);
    yoseob("task thread %d end. status : %d\n", i, status);
  }

  this_->thread_ids.clear(); // clear all datas
  this_->datas.clear();
 
  return true;
#endif  
}

bool Timer::schedule(CommonTask* task, int period)
{
  TimerImpl* this_ = (TimerImpl *)pImpl;
#ifdef __linux__
  yoseob("schedule tasks using epoll\n");
  // Create and setting timer fd
  int timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
  if(timer_fd == -1)
  {
    perror("timerfd_create");
    assert(0);
    return false;
  }

  epoll_event ev_timer; // create timer event
  ev_timer.events = EPOLLIN | EPOLLPRI;
  ev_timer.data.fd = timer_fd;
  if(epoll_ctl(this_->epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev_timer) == -1)
  {
    perror("epoll_ctl: timer_fd");
    assert(0);
    return false;
  }
 
  struct itimerspec period_value;
  struct timespec cur_time;

  if(clock_gettime(CLOCK_REALTIME, &cur_time) == -1)
  {
    perror("clock_gettime");
    assert(0);
    return false;
  } 

  period_value.it_value.tv_sec = cur_time.tv_sec; // fire now
  period_value.it_value.tv_nsec = cur_time.tv_nsec;
  period_value.it_interval.tv_sec = period;
  period_value.it_interval.tv_nsec = 0;

  if(timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &period_value, NULL) == -1)
  {  
    perror("timerfd_settime");
    assert(0);
    return false;
  }

  timer_data_t t_data;
  t_data.timer_fd = timer_fd;
  t_data.task = task;
  this_->datas.push_back(t_data);

  return true;
#else
  pthread_t tid;
  timer_data_t* t_data = new timer_data_t(); // for task thread
  t_data->period = period;
  t_data->task = task;
  yoseob("schedule tasks using thread\n");

  pthread_mutex_lock(&t_data->mutex);
  if(pthread_create(&tid, NULL, timer_thread_func, (void *)t_data) < 0)
  {
    perror("pthread_create");
    assert(0);
    pthread_mutex_unlock(&t_data->mutex);
    return false;
  }
  pthread_cond_wait(&t_data->cond, &t_data->mutex);
  pthread_mutex_unlock(&t_data->mutex);

  this_->thread_ids.push_back(tid);
  this_->datas.push_back(t_data);

  return true;
#endif
}

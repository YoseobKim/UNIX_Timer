#ifndef TESTAPP_TIMER_H_
#define TESTAPP_TIMER_H_

class CommonTask;

class Timer
{
 public :
  Timer();
  ~Timer();
  bool starttimer(); // start timer for schedule.
  bool stoptimer(); // stop all schedules and destroy timer.
  bool schedule(CommonTask* task, int period);
 private :
  void* pImpl = nullptr;
};

#endif // end of TESAPP_TIMER_H_

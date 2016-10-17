#ifndef TESTAPP_COMMONTASK_H_
#define TESTAPP_COMMONTASK_H_

class CommonTask
{
 public :
  explicit CommonTask() { };
  virtual ~CommonTask() { };
  virtual void execute(void) = 0;
};

#endif // end of TESAPP_COMMONTASK_H_

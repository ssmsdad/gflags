

#include <iostream>
#include <gflags/gflags.h>
using namespace std;
 
DEFINE_bool(isvip, true, "If Is VIP");
DEFINE_string(ip, "127.0.0.1", "connect ip");
DECLARE_int32(port);
DEFINE_int32(port, 80, "listen port");
 
int main(int argc, char** argv)
{
  // 在调用ParseCommandLineFlags后，gflags会将命令行参数中的flag解析到对应的全局变量中
  // 如：./testgflags --ip=127.0.0.1，解析后FLAGS_ip=127.0.0.1
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::cout<<"ip:"<<FLAGS_ip<<std::endl;
  std::cout<<"port:"<<FLAGS_port<<std::endl;
  if (FLAGS_isvip)
  {
      std::cout<<"isvip:"<<FLAGS_isvip<<std::endl;
  }
  gflags::ShutDownCommandLineFlags();
  return 0;
}
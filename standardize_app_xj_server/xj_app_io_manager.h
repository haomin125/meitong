#ifndef XJ_APP_IO_MANAGER_H
#define XJ_APP_IO_MANAGER_H

  #include "io_card_manager.h"
  #include "io_tcpip_modbus_manager.h"

  class AppIoManagerIOCard : public AdvtechDirectIOBoardManager
  {
  public:
      AppIoManagerIOCard(const std::string &name) :
      AdvtechDirectIOBoardManager(name)
      {
          init();
      }
      virtual ~AppIoManagerIOCard() {};

      virtual int init();
  };


  class AppIoManagerPLC : public TcpipModbusManager
  {
  public:
      AppIoManagerPLC(const std::string &serverIP, int serverPort);
      AppIoManagerPLC(const std::string &serverIP, int serverPort, const int sendRecvTimeout);
      virtual ~AppIoManagerPLC() {};

  };


#endif // XJ_APP_IO_MANAGER_H

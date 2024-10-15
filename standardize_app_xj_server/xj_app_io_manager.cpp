#include "xj_app_io_manager.h"

//#ifdef USE_IO_CARD
	// app need to define its own io manager if it does not use SeekingIoManager from framework
	int AppIoManagerIOCard::init()
	{
		// please add your own code if necessary
		return 0;
	}
	
//#else
	AppIoManagerPLC::AppIoManagerPLC(const std::string &serverIP, int serverPort):
		TcpipModbusManager(serverIP, serverPort)
	{
	}

	AppIoManagerPLC::AppIoManagerPLC(const std::string &serverIP, int serverPort, const int sendRecvTimeout):
		TcpipModbusManager(serverIP, serverPort, sendRecvTimeout)
	{

	}

//#endif //USE_IO_CARD
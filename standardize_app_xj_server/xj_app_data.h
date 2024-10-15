#ifndef XJ_APP_DATA_H
#define XJ_APP_DATA_H

//运行状态：1-运行 2-测试
enum class RunStatus: int
{
	PRODUCT_RUN = 1,
	TEST_RUN 	= 2
};


//图像保存方式： 0-保存所有 1-只存NG 2-不保存
enum class SaveImageType: int
{
	ALL         =  0,
	NG_ONLY     =  1,
	NO          =  2,
	BOARD_START = 3       // 工位存图的起始ID 即 3表示只存board0所有图片， 4表示只存board1所有图片,以此类推...
};

//图像类型
enum class ImageType: int
{
	ORIGIN       =  0,
	GOOD         =  1,
	BAD          =  2,
	GOOD_RESULT  =  3,
	BAD_RESULT   =  4,
};

//剔除模式
enum class PurgeMode : int
{
	NORMAL	= 0,
	ALL_OK	= 1,
	ALL_NG	= 2,
    OK_NG   = 3                 
};

//PLC信号
enum class PLCSinal: int
{
	OK = 1,
	NG = 2
};

//运行模式：0-检测  1-空跑
enum class RunMode : int
{
	RUN_DETECT	= 0,
	RUN_EMPTY	= 1  
};

//浮点类型参数存储索引
enum class ProductSettingFloatMapper : int
{
	SAVE_IMAGE_TYPE     				= 0, 	// save a image for a given type 0, 1, and 2; 0 means all, 1 means only save defect, and 2 means not save at all
	IMAGE_SAVE_SIZE 					= 1,
	MODEL_TYPE 							= 2,
	SENSITIVITY 						= 3, 	// the sensitivity of the model, range (0, 1)
	ALARM_THRESHOLD 					= 4,   // the threshold for the alarm to kick defects out, range (0, 1]
	PRESSURE_THRESHOLD 					= 5,  // the pressure alarm value, range (0, 1]
	PURGE_SIGNAL_TYPE                   = 6, //purge mode: 0-normal, 1-ok, 2-ng, 3-ok&ng
	RUN_MODE                            = 7,
};

enum class TestProcessPhase : int
{
	NO_PROCESS = 0,
	PRE_PROCESS_ONLY = 1,
	PRE_PROCESS_AND_CLASSIFY = 2,
	AUTO_UPDATE_PARAMS_FINISH = 300
};

//拍照次数
enum class CaptureImageTimes : int
{
	UNKNOWN_TIMES = 0,
	FIRST_TIMES   = 1,
	SECOND_TIMES  = 2,
	THIRD_TIMES   = 3,
};


#endif
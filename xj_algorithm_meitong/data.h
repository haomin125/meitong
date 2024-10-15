#ifndef DATA_H
#define DATA_H 




// enum DefectType:int
// {
//     GOOD = 1,
// 	LOSS = 2,
// 	WUBIAO_CUOBIAO = 3,
// 	OTHER = 4
// };


//拍照次数
enum class CaptureImageTimes : int
{
	UNKNOWN_TIMES = 0,
	FIRST_TIMES   = 1,
	SECOND_TIMES  = 2,
};

//图像保存方式： 0-保存所有 1-只存NG 2-不保存
enum class SaveImageType: int
{
	ALL         =  0,
	NG_ONLY     =  1,
	NO          =  2
};

enum class FloatParamIndex : int
{

	IS_CHECK_WUTIAOXINGMA							= 56,
	TIAOXINGMA_AREA_THRESHOLD						= 57,

	IS_CHECK_PINGKAYICHANG							= 127,
	KONG_GRAY_THRESHOLD              			   	= 128,
	PINGMIAN_GRAY_THRESHOLD          			   	= 129,

	IS_CHECK_MIANZHIHUNLIAO							= 90,

	CAM0_IS_CHECK_LOGO								= 53,
	CAM1_IS_CHECK_LOGO								= 73,
};


#endif	//DATA_H 

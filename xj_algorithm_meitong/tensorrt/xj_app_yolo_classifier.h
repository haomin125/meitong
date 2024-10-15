#ifndef XJ_APP_YOLO_CLASSIFIERH
#define XJ_APP_YOLO_CLASSIFIERH
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "cuda_runtime_api.h"
#include "NvInfer.h"
#include <functional>
#include <mutex>
#define BINDING_SIZE 3

enum  class YoloOutputType : int
{
    CATEGORY          = 1,
    DETECTION         = 2,
    DETECTION_SEGMENT = 3
};

class LoggerNV_yolo : public nvinfer1::ILogger
{
public:
    static LoggerNV_yolo &instance()
    {
        static LoggerNV_yolo instance;
        return instance;
    }
private:
    LoggerNV_yolo() {}
    virtual ~LoggerNV_yolo() {}
    using Severity = nvinfer1::ILogger::Severity;
    void log(Severity severity, const char *msg) noexcept override
    {
        // suppress info-level messages
        if (severity <= Severity::kWARNING)
        {
            std::cout << msg << std::endl;
        }
    }
};

struct YoloOutputSeg {
	int id;             //结果类别id
	float confidence;   //结果置信度
	cv::Rect box;       //矩形框
	cv::Mat boxMask;    //矩形框内mask，节省内存空间和加快速度
};

struct YoloOutputDetect {
	int id;             //结果类别id
	float confidence;   //结果置信度
	cv::Rect box;       //矩形框
};

class YoloClassifier
{
public:
    /**
     * @brief 构造函数 
     * @param[in] {sModelPath 模型路径}
     * @param[in] {yolotensorrtOutputType 模型输出类型如:分类,分割,多任务}
     * @param[in] {batchSize 模型一次推理多少张图片}
     * @param[in] {numCategory 模型分类数}
     * @param[in] {inputWidth 模型输入图片宽度}
     * @param[in] {inputHeight 模型输入图片高度}
     * @param[in] {inputChannel 模型输入图片通道数(默认３通道)}
     */    
    YoloClassifier(const std::string &sModelPath,const YoloOutputType yolotensorrtOutputType,const int batchSize,const int numCategory,const int inputWidth, const int inputHeight, const int inputChannel = 3);
    ~YoloClassifier();
    /**
     * @brief 加载模型
     * @return {模型加载是否成功}
     */    
    bool loadModel(); 
    /**
     * @brief  设置模型参数
     * @param {yolotensorrtOutputType 模型输出格式}
     * @param {batchSize 一次推理大小}
     * @param {numCategory 模型分类数}
     * @param {inputWidth 输入图片宽度}
     * @param {inputHeight 输入图片高度}
     * @param {inputChannel 输入图片通道数}
     */     
    void setModelParameters(const YoloOutputType yolotensorrtOutputType,const int batchSize,const int numCategory,const int inputWidth,const int inputHeight, const int inputChannel);   
    /**
     * @brief  设置目标检测超参数
     * @param {conf_th 分割分数阈值}
     * @param {nms_th  目标检测矩形框阈值}
     * @param {mask_th 分割mask图片阈值}
     * @param {seg_scalefactor 分割缩放比例(比较固定的模型参数)}
     * @param {seg_channels 分割通道数(比较固定的模型参数)}
     * @param {detbox_num 矩形框数量(比较固定的模型参数)}
     */ 
    void setInferParameters(const float conf_th, const float nms_th, const float mask_th,const int seg_scalefactor,const int seg_channels,const int detbox_num);
    /**
     * @brief 获取batchSize
     * @return {返回batchSize}
     */    
    int getBatchSize(){return m_batch_size;};
    /**
     * @brief 获取输入图片宽度
     * @return {返回输入图片宽度}
     */ 
    int getImageWidth(){return m_input_w;};
    /**
     * @brief 获取输入图片高度
     * @return {返回输入图片高度}
     */ 
    int getImageHeight(){return m_input_h;};
    /**
     * @brief 获取模型分类数
     * @return {返回模型分类数}
     */ 
    int getImageCategory(){return m_numCategory;};

    bool getDetectionResult(const  std::vector<cv::Mat> &vBatchImage, std::vector<std::vector<YoloOutputDetect>> &vDetectOutput);
    /**
     * @brief 分割后处理(适用于纯分割模型)
     * @param[in]  {vBatchImage  　输入图片}
     * @param[out] {vInstanceSegOutput 输出分割mask结果图}
     * @return {模型分割推理是否成功}
     */ 
    bool getInstanceSegmentResult(const std::vector<cv::Mat> &vBatchImage, std::vector<std::vector<YoloOutputSeg>> &vInstanceSegOutput);
private:
    /**
     * @brief 创建或者拷贝内存
     * @param[in] {method 选择创建还是拷贝内存方法}
     * @return {内存操作是否成功}
     */    
    bool createOrCopyBuffers(const int method);
    //判断engine模型是否为动态模型
    /**
     * @brief 判断engine模型是否为动态模型
     * @return {返回模型是否为动态模型}
     */    
    bool hasDynamicDim();
    /**
     * @brief 根据动态batch size重新设置input dim维度 
     * @param[in] {ibinding 模型的ibinding}
     * @param[in] {dims     模型的维度}
     * @return {是否设置成功}
     */    
    bool setRunDims(int ibinding,const std::vector<int> & dims);
    /**
     * @brief 对分类结果做softmax
     * @param[in] {src 输入数据}
     * @param[out] {dst 输出数据}
     * @param[in] {numCategory 模型分类数}
     */    
    template<typename T>
    void softmax(const T* src,T * dst,const int numCategory);
    /**
     * @brief 打印模型内部参数
     */    
    void print();
    
protected:
     /**
     * @brief 按路径加载模型数据
     * @param {file 文件路径}
     * @return {模型数据}
     */ 
    std::vector<unsigned char> load_file(const std::string& file);
    /**
     * @brief 对输入图片进行归一化
     * @param[in] {srcImg 输入图片}
     * @param[out] {dstImg 输出图片}
     */    
    // void normalizeImage(const cv::Mat &srcImg, cv::Mat &dstImg);
    virtual std::vector<int> normalizeImage(const cv::Mat &srcImg, cv::Mat &dstImg);
    /**
     * @brief 模型推理 
     * @param[in]  {vBatchImage  　输入图片}
     * @return {推理是否成功}
     */ 
    virtual bool inference(const std::vector<cv::Mat> &vBatchImage);
    
    /**
     * @brief 目标检测预处理矩形框
     * @param[in]   {src 　　　输入图片}
     * @param[out] {dst 　　　输出图片}
     * @param[in]  {dsize 　　输入图片大小}
     * @param[in]  {bgcolor 　输入画框颜色}
     * @return {矩形参数}
     */ 
    virtual std::vector<int> letterbox(const cv::Mat& src, cv::Mat& dst,const cv::Size & dsize, const cv::Scalar & bgcolor);
    /**
     * @brief 根据分割推理结果绘制矩形框
     * @param[in]   {src  　输入待绘制图片}
     * @param[in]   {result 模型推理检测矩形}
     * @param[in]   {c_class　通道数}
     * @return {无}
     */ 
    virtual void drawPred(cv::Mat& src,const std::vector<YoloOutputSeg> & result,const int c_class);
    /**
     * @brief 将box转换成矩形
     * @param[in]   {src  　输入图片}
     * @param[in]   {bbox[4]  矩形4个顶点}
     * @param[in]   {INPUT_W　图片宽度}
     * @param[in]   {INPUT_H　图片高度}
     * @return {无}
     */ 
    virtual cv::Rect getRect(const cv::Mat& src, float bbox[4],const int INPUT_W,const int INPUT_H);
private:
    std::string m_sModelPath;//模型路径
    cudaStream_t m_stream;
    std::shared_ptr<nvinfer1::IRuntime> m_runtime;
    std::shared_ptr<nvinfer1::ICudaEngine> m_engine;
    std::shared_ptr<nvinfer1::IExecutionContext> m_context;
    float* m_buffers[BINDING_SIZE] = {nullptr}; // cpu buffers for input and output data
    void* m_gpu_buffers[BINDING_SIZE] = {nullptr};; // gpu buffers for input and output data
    //模型参数
    int m_batch_size;
    int m_numCategory;
    int m_input_w;
    int m_input_h;
    int m_input_c;
    YoloOutputType m_yoloOutputType;//模型输出类型
    //比较固定的模型参数
    int m_seg_scalefactor ;
    int m_seg_channels ;
    int m_detbox_num ;

    //超参数
    float m_conf_th = 0.3;
    float m_nms_th = 0.5;
    float m_mask_th = 0.5;
    cv::Scalar m_bg_color = cv::Scalar(128, 128, 128);
    //temp参数
    std::vector<std::vector<int>> m_vPadsize; //保存letterbox的pading参数,用完需要马上clear
};


#endif //TENSORRT_H
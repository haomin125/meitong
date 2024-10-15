#include "xj_app_yolo_classifier.h"
#include "logger.h"
#include <iostream>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <cmath>
#include "opencv_utils.h"

using namespace std;
using namespace cv;

#define INPUT_INDEX 0
#define CLS_OR_DETECTION_OUTPUT_INDEX 1
#define DETECTION_AND_SEG_OUTPUT_INDEX 2
#define CREATE_BUFFERS   10
#define COPY_BUFFERS     11
#define CHECK(status) \
    do\
    {\
        auto ret = (status);\
        if (ret != 0)\
        {\
            std::cerr << "Cuda failure: " << ret << std::endl;\
            abort();\
        }\
    } while (0)

template <typename _T>
shared_ptr<_T> make_nvshared(_T *ptr) {
	return shared_ptr<_T>(ptr, [](_T* p){p->destroy();});
}
    
vector<unsigned char> YoloClassifier::load_file(const string& file) 
{                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    vector<uint8_t> data;
	ifstream in(file, ios::in | ios::binary);
	if (!in.is_open())                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {
        return data;
    }
	in.seekg(0, ios::end);
	size_t length = in.tellg();
	if (length > 0) 
    {
		in.seekg(0, ios::beg);
		data.resize(length);
		in.read((char*)&data[0], length);
	}
	in.close();
	return data;
}

YoloClassifier::YoloClassifier(const string &sModelPath,const YoloOutputType yolotensorrtOutputType, const int batchSize, const int numCategory, const int inputWidth, const int inputHeight, const int inputChannel):
                m_sModelPath(sModelPath),
                m_yoloOutputType(yolotensorrtOutputType),
                m_batch_size(batchSize),
                m_numCategory(numCategory),
				m_input_w(inputWidth),
				m_input_h(inputHeight),
				m_input_c(inputChannel),
                m_runtime(nullptr),
                m_engine(nullptr),
                m_context(nullptr)
{

}

YoloClassifier::~YoloClassifier()
{
    if (m_stream != NULL)
        cudaStreamDestroy(m_stream);
    for (int i = 0; i < BINDING_SIZE ; i++)
    {
        if (m_buffers[i] != nullptr)
        {
            free(m_buffers[i]);
        }
        if (m_gpu_buffers[i] != nullptr)
        {
            CHECK(cudaFree(m_gpu_buffers[i]));
        }
    }
}

bool YoloClassifier::createOrCopyBuffers(const int method)
{
    //step5:申请输出内存和显存
    std::vector<int> vOutputSize;
    int number = CLS_OR_DETECTION_OUTPUT_INDEX;
    auto cls_output_size = m_numCategory * m_batch_size * sizeof(float);
    auto detection_output_size = m_detbox_num * (m_numCategory + 4) * m_batch_size * sizeof(float);
    //判断是分类、检测 或 检测+分割
    if (YoloOutputType::CATEGORY == m_yoloOutputType)
    {
        vOutputSize.emplace_back(cls_output_size);
    } 
    else if (YoloOutputType::DETECTION_SEGMENT == m_yoloOutputType)
    {
        auto seg_output_size = m_seg_channels * m_input_w * m_input_h / m_seg_scalefactor / m_seg_scalefactor * m_batch_size * sizeof(float);
        number = DETECTION_AND_SEG_OUTPUT_INDEX;
        detection_output_size = m_detbox_num * (m_numCategory + 4 + m_seg_channels) * m_batch_size * sizeof(float);
        vOutputSize.emplace_back(detection_output_size);
        vOutputSize.emplace_back(seg_output_size); 
    } else
    {
        vOutputSize.emplace_back(detection_output_size);
    }
    //判断拷贝还是创建
    for (size_t i = 1; i <= number; i++)
    {
        if (method == COPY_BUFFERS)
        {
            CHECK(cudaMemcpyAsync(m_buffers[i], m_gpu_buffers[i],vOutputSize.at(i - 1),cudaMemcpyDeviceToHost, m_stream));
        } else
        {
            m_buffers[i] = (float*)malloc(vOutputSize.at(i - 1));
            CHECK(cudaMalloc(&m_gpu_buffers[i], vOutputSize.at(i - 1)));
        }
    }
    return true;
}

bool YoloClassifier::hasDynamicDim()
{
    int numBindings = m_engine->getNbBindings();
    for (int i = 0; i < numBindings; i++)
    {
        nvinfer1::Dims dims = m_engine->getBindingDimensions(i);
        for (int j = 0; j < dims.nbDims; ++j)
        {
            if (dims.d[j] == -1)
                return true;
        }
    }
    return false;
}

bool YoloClassifier::setRunDims(int ibinding,const std::vector<int> & dims)
{
    nvinfer1::Dims d;
    std::memcpy(d.d,dims.data(),sizeof(int) * dims.size());
    d.nbDims = dims.size();
    return m_context->setBindingDimensions(ibinding,d);
}

template<typename T>
void YoloClassifier::softmax(const T* src,T * dst,const int numCategory)
{
    const T max_val = *std::max_element(src,src + numCategory);
    T denominator{0};
    for (auto i = 0; i < numCategory; i++)
    {
        dst[i] = std::exp(src[i] - max_val);
        denominator += dst[i];
    }
    for (auto i = 0; i < numCategory; i++)
    {
        dst[i] /= denominator;
    }
}

bool YoloClassifier::loadModel()
{     
    //step1: set gpu device No.
    cudaSetDevice(0);
    //step2:load file
    vector<unsigned char> engine_data = load_file(m_sModelPath);
    if (engine_data.size() == 0)
    {
        LogERROR << "engine model is empty!!!";  
        return false;
    }
    //step3:deserialize the engine
    m_runtime.reset(nvinfer1::createInferRuntime(LoggerNV_yolo::instance()));
    m_engine.reset(m_runtime->deserializeCudaEngine(engine_data.data(), engine_data.size(), nullptr));
    m_context.reset(m_engine->createExecutionContext());
    //step4:申请输入内存和显存
    CHECK(cudaStreamCreate(&m_stream));
    auto input_size = m_input_w * m_input_h * m_input_c * m_batch_size * sizeof(float);
    m_buffers[INPUT_INDEX] = (float*)malloc(input_size);
    CHECK(cudaMalloc(&m_gpu_buffers[INPUT_INDEX], input_size));
    // YoloClassifier::print();
    if (hasDynamicDim())
        setRunDims(0,{m_batch_size,m_input_c,m_input_h,m_input_w});
    //step5:申请输出内存和显存
    createOrCopyBuffers(CREATE_BUFFERS);
    //step6:推理预热
    vector<Mat> vecImages;
    Mat image = Mat::zeros(Size(m_input_h, m_input_w), CV_8UC3);
    for (int i=0; i<m_batch_size; i++) { vecImages.push_back(image); }
    inference(vecImages);
    return true;
}

void YoloClassifier::setModelParameters(const YoloOutputType yolotensorrtOutputType,
                                        const int batchSize, 
                                        const int numCategory, 
                                        const int inputWidth, 
                                        const int inputHeight, 
                                        const int inputChannel)
{
    m_yoloOutputType = yolotensorrtOutputType;
    m_batch_size = batchSize;
    m_numCategory = numCategory;
    m_input_w = inputWidth;
	m_input_h = inputHeight;
	m_input_c = inputChannel;
}

void YoloClassifier::setInferParameters(const float conf_th, 
                                        const float nms_th, 
                                        const float mask_th,
                                        const int seg_scalefactor,
                                        const int seg_channels,
                                        const int detbox_num)
{
    m_conf_th = conf_th;
    m_nms_th  = nms_th;
    m_mask_th = mask_th;
    m_seg_scalefactor = seg_scalefactor;
    m_seg_channels = seg_channels;
    m_detbox_num = detbox_num;
}

std::vector<int> YoloClassifier::normalizeImage(const Mat &srcImg, Mat &dstImg)
{
    vector<int> vPadsize = letterbox(srcImg, dstImg, Size(m_input_w, m_input_h), m_bg_color);
    //step2:BGR转RGB
	cvtColor(dstImg, dstImg, COLOR_BGR2RGB);
    //step3:归一化到0-1
	dstImg.convertTo(dstImg, CV_32FC3, 1.0/255);
    return vPadsize;
}

bool YoloClassifier::inference(const vector<Mat> &vBatchImage)
{
    if (vBatchImage.empty())
    {
        LogERROR << "inference no images input!!!";
        return false;
    }
    m_vPadsize.clear();
    //step1:对图像进行前处理，并将图像拷贝到指针cpu数组m_buffers[0]当中
    for (size_t i = 0; i < vBatchImage.size(); ++i)
    {
        Mat inputImg;
        m_vPadsize.emplace_back(normalizeImage(vBatchImage[i], inputImg));
        vector<Mat> chw;
        for (size_t j = 0; j < m_input_c; ++ j) 
        {
           chw.emplace_back(cv::Mat(Size(m_input_w, m_input_h), CV_32FC1, 
                            (float*)m_buffers[INPUT_INDEX] + (j + i * m_input_c) * m_input_w * m_input_h));
        }
        split(inputImg, chw);
    }
    //step2:从内存到显存, 从CPU到GPU, 将输入数据拷贝到显存
    CHECK(cudaMemcpyAsync(m_gpu_buffers[INPUT_INDEX], 
                          m_buffers[INPUT_INDEX],
                          m_input_w * m_input_h * m_input_c * m_batch_size * sizeof(float),
                          cudaMemcpyHostToDevice, m_stream));
    //step3:推理
    m_context->enqueueV2(m_gpu_buffers, m_stream, nullptr);
    //step4:显存拷贝到内存
    createOrCopyBuffers(COPY_BUFFERS);
    //step5:这个是为了同步不同的流
    cudaStreamSynchronize(m_stream);
    return true;
}

void YoloClassifier::print() 
{
    int num_input = 0;
    int num_output = 0;
    auto engine = this->m_engine;
    for (int i = 0; i < engine->getNbBindings(); ++i) {
        if (engine->bindingIsInput(i))
            num_input++;
        else
            num_output++;
    }

    printf("Inputs: %d", num_input);
    for (int i = 0; i < num_input; ++i) {
        auto name = engine->getBindingName(i);
        auto dim = engine->getBindingDimensions(i);
        printf("\t%d.%s", i, name);
        for (int j = 0; j < 4; j++)
        {
            //LogINFO<<dim.d[j]<<" ";
        }
    }

    printf("Outputs: %d", num_output);
    for (int i = 0; i < num_output; ++i) {
        auto name = engine->getBindingName(i + num_input);
        auto dim = engine->getBindingDimensions(i + num_input);
        printf("\t%d.%s : shape {%s}", i, name);
        for (int j = 0; j < 4; j++)
        {
            //LogINFO<<dim.d[j]<<" ";
        }
    }
}

std::vector<int> YoloClassifier::letterbox(const cv::Mat& src, cv::Mat& dst,const cv::Size & dsize, const cv::Scalar & bgcolor) {
	int w, h, x, y;
	float r_w = dsize.width / (src.cols*1.0);
	float r_h = dsize.height / (src.rows*1.0);
	if (r_h > r_w) {
		w = dsize.width;
		h = r_w * src.rows;
		x = 0;
		y = (dsize.height - h) / 2;
	}
	else {
		w = r_h * src.cols;
		h = dsize.height;
		x = (dsize.width - w) / 2;
		y = 0;
	}
	cv::Mat re(h, w, CV_8UC3);
	cv::resize(src, re, re.size(), 0, 0, cv::INTER_LINEAR);
	cv::Mat out(dsize.height, dsize.width, CV_8UC3, bgcolor);
    out.copyTo(dst);
	re.copyTo(dst(cv::Rect(x, y, re.cols, re.rows)));

	std::vector<int> vPadsize;
	vPadsize.emplace_back((int)h);
	vPadsize.emplace_back((int)w);
	vPadsize.emplace_back((int)y);
	vPadsize.emplace_back((int)x);
	vPadsize.emplace_back((int)src.rows);
	vPadsize.emplace_back((int)src.cols);

	return vPadsize;
}

cv::Rect YoloClassifier::getRect(const cv::Mat& src, float bbox[4],const int INPUT_W,const int INPUT_H) {
	int l, r, t, b;
	float r_w = INPUT_W / (src.cols * 1.0);
	float r_h = INPUT_H / (src.rows * 1.0);
	if (r_h > r_w) {
		l = bbox[0];
		r = bbox[2];
		t = bbox[1]- (INPUT_H - r_w * src.rows) / 2;
		b = bbox[3] - (INPUT_H - r_w * src.rows) / 2;
		l = l / r_w;
		r = r / r_w;
		t = t / r_w;
		b = b / r_w;
	}
	else {
		l = bbox[0] - bbox[2] / 2.f - (INPUT_W - r_h * src.cols) / 2;
		r = bbox[0] + bbox[2] / 2.f - (INPUT_W - r_h * src.cols) / 2;
		t = bbox[1] - bbox[3] / 2.f;
		b = bbox[1] + bbox[3] / 2.f;
		l = l / r_h;
		r = r / r_h;
		t = t / r_h;
		b = b / r_h;
	}
	return cv::Rect(l, t, r - l, b - t);
}

void YoloClassifier::drawPred(Mat& src,const std::vector<YoloOutputSeg> & result,const int c_class) 
{
	//生成随机颜色
	std::vector<Scalar> color;
	srand(time(0));
	for (int i = 0; i < c_class; i++) {
		int b = rand() % 256;
		int g = rand() % 256;
		int r = rand() % 256;
		color.emplace_back(Scalar(b, g, r));
	}
	Mat mask = src.clone();
	for (int i = 0; i < result.size(); i++) {
		int left, top;
		left = result[i].box.x;
		top = result[i].box.y;
		int color_num = i;
		rectangle(src, result[i].box, color[result[i].id], 2, 8);
		
		mask(result[i].box).setTo(color[result[i].id], result[i].boxMask);
		char label[100];
		sprintf(label, "%d:%.2f", result[i].id, result[i].confidence);

		//std::string label = std::to_string(result[i].id) + ":" + std::to_string(result[i].confidence);
		int baseLine;
		Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
		top = max(top, labelSize.height);
		putText(src, label, Point(left, top), FONT_HERSHEY_SIMPLEX, 1, color[result[i].id], 2);
	}
	
	addWeighted(src, 0.5, mask, 0.8, 1, src); //将mask加在原图上面
}

bool YoloClassifier::getDetectionResult(const vector<Mat> &vBatchImage, vector<vector<YoloOutputDetect>> &vDetectOutput)
{
    //step1: inference
    auto start = std::chrono::system_clock::now();
    if (!inference(vBatchImage))
    {
        cout << "tensorrt dl inference failed!!!" << endl;
        return false;
    }
    auto end = std::chrono::system_clock::now();
	// std::cout << "推理时间：" << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    //step2: post-process
    start = std::chrono::system_clock::now();
    for (int i = 0; i < vBatchImage.size(); ++i)
	{
        std::vector<int> vClassIds;//结果id数组
	    std::vector<float> vConfidences;//结果每个id对应置信度数组
	    std::vector<cv::Rect> vBoxes;//每个id矩形框
        std::vector<YoloOutputDetect> vOutput;

        // 处理box
        int net_length = m_numCategory + 4;
        cv::Mat detectionOut = cv::Mat(net_length, m_detbox_num, CV_32F, (float*)m_buffers[CLS_OR_DETECTION_OUTPUT_INDEX] + i * m_detbox_num * net_length);
        // padsize参数
        int newh = m_vPadsize[i][0];
        int neww = m_vPadsize[i][1];
        int padh = m_vPadsize[i][2];
        int padw = m_vPadsize[i][3];
        int img_h = m_vPadsize[i][4];
	    int img_w = m_vPadsize[i][5];
        float ratio_h = (float)img_h / newh;
	    float ratio_w = (float)img_w / neww;
        for (int j = 0; j < m_detbox_num; j++) {
            //输出是1*net_length*m_detbox_num;所以每个box的属性是每隔m_detbox_num取一个值，共net_length个值
            cv::Mat scores = detectionOut(Rect(j, 4, 1, m_numCategory)).clone();
            Point classIdPoint;
            double max_class_socre;
            minMaxLoc(scores, 0, &max_class_socre, 0, &classIdPoint);
            max_class_socre = (float)max_class_socre;
            if (max_class_socre >= m_conf_th) {
                float x = (detectionOut.at<float>(0, j) - padw) * ratio_w;  //cx
                float y = (detectionOut.at<float>(1, j) - padh) * ratio_h;  //cy
                float w = detectionOut.at<float>(2, j) * ratio_w;  //w
                float h = detectionOut.at<float>(3, j) * ratio_h;  //h
                int left = MAX((x - 0.5 * w), 0);
                int top = MAX((y - 0.5 * h), 0);
                int width = (int)w;
                int height = (int)h;
                if (width <= 0 || height <= 0) { continue; }

                vClassIds.emplace_back(classIdPoint.y);
                vConfidences.emplace_back(max_class_socre);
                vBoxes.emplace_back(Rect(left, top, width, height));
            }
        }
        //执行非最大抑制以消除具有较低置信度的冗余重叠框（NMS）
        std::vector<int> nms_result;
        cv::dnn::NMSBoxes(vBoxes, vConfidences, m_conf_th, m_nms_th, nms_result);
        if (nms_result.empty())
        {
            vDetectOutput.emplace_back(vOutput);
            return true;
        }
        
        Rect holeImgRect(0, 0, img_w, img_h);
        for (int j = 0; j < nms_result.size(); ++j) {
            int idx = nms_result[j];
            YoloOutputDetect result;
            result.id = vClassIds[idx];
            result.confidence = vConfidences[idx];
            result.box = vBoxes[idx]& holeImgRect;
            vOutput.emplace_back(result);
        }
        vDetectOutput.emplace_back(vOutput);
    }
    end = std::chrono::system_clock::now();
    // std::cout << "后处理时间：" << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;

    return true;
}

bool YoloClassifier::getInstanceSegmentResult(const vector<Mat> &vBatchImage, vector<vector<YoloOutputSeg>> &vInstanceSegOutput)
{
    //step1: inference
    auto start = std::chrono::system_clock::now();
    if (!inference(vBatchImage))
    {
        cout << "tensorrt dl inference failed!!!" << endl;
        return false;
    }
    auto end = std::chrono::system_clock::now();
	// std::cout << "推理时间：" << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    //step2: post-process
    start = std::chrono::system_clock::now();
    for (int i = 0; i < vBatchImage.size(); ++i)
	{
        std::vector<int> vClassIds;//结果id数组
	    std::vector<float> vConfidences;//结果每个id对应置信度数组
	    std::vector<cv::Rect> vBoxes;//每个id矩形框
	    std::vector<cv::Mat> vPicked_proposals;  //后续计算mask

        // 处理box
        int net_length = m_numCategory + 4 + m_seg_channels;
        cv::Mat detectionOut = cv::Mat(net_length, m_detbox_num, CV_32F, (float*)m_buffers[DETECTION_AND_SEG_OUTPUT_INDEX] + i * m_detbox_num * net_length);
        // padsize参数
        int newh = m_vPadsize[i][0];
        int neww = m_vPadsize[i][1];
        int padh = m_vPadsize[i][2];
        int padw = m_vPadsize[i][3];
        int img_h = m_vPadsize[i][4];
	    int img_w = m_vPadsize[i][5];
        float ratio_h = (float)img_h / newh;
	    float ratio_w = (float)img_w / neww;
        for (int j = 0; j < m_detbox_num; j++) {
            //输出是1*net_length*m_detbox_num;所以每个box的属性是每隔m_detbox_num取一个值，共net_length个值
            cv::Mat scores = detectionOut(Rect(j, 4, 1, m_numCategory)).clone();
            Point classIdPoint;
            double max_class_socre;
            minMaxLoc(scores, 0, &max_class_socre, 0, &classIdPoint);
            max_class_socre = (float)max_class_socre;
            if (max_class_socre >= m_conf_th) {
                cv::Mat temp_proto = detectionOut(Rect(j, 4 + m_numCategory, 1, m_seg_channels)).clone();
                vPicked_proposals.emplace_back(temp_proto.t());
                float x = (detectionOut.at<float>(0, j) - padw) * ratio_w;  //cx
                float y = (detectionOut.at<float>(1, j) - padh) * ratio_h;  //cy
                float w = detectionOut.at<float>(2, j) * ratio_w;  //w
                float h = detectionOut.at<float>(3, j) * ratio_h;  //h
                int left = MAX((x - 0.5 * w), 0);
                int top = MAX((y - 0.5 * h), 0);
                int width = (int)w;
                int height = (int)h;
                if (width <= 0 || height <= 0) { continue; }

                vClassIds.emplace_back(classIdPoint.y);
                vConfidences.emplace_back(max_class_socre);
                vBoxes.emplace_back(Rect(left, top, width, height));
            }
        }
        //执行非最大抑制以消除具有较低置信度的冗余重叠框（NMS）
        std::vector<int> nms_result;
        cv::dnn::NMSBoxes(vBoxes, vConfidences, m_conf_th, m_nms_th, nms_result);
        if (nms_result.empty())
        {
            return true;
        }
        std::vector<cv::Mat> vTemp_mask_proposals;
        std::vector<YoloOutputSeg> vOutput;
        Rect holeImgRect(0, 0, img_w, img_h);
        for (int j = 0; j < nms_result.size(); ++j) {
            int idx = nms_result[j];
            YoloOutputSeg result;
            result.id = vClassIds[idx];
            result.confidence = vConfidences[idx];
            result.box = vBoxes[idx]& holeImgRect;
            vOutput.emplace_back(result);
            vTemp_mask_proposals.emplace_back(vPicked_proposals[idx]);
        }
        // 处理mask
        Mat maskProposals;
        for (int j = 0; j < vTemp_mask_proposals.size(); ++j)
            maskProposals.push_back(vTemp_mask_proposals[j]);

        int segWidth =  int(m_input_w / m_seg_scalefactor);
        int segHeight = int(m_input_h / m_seg_scalefactor);
       
        Mat protos = Mat(m_seg_channels, segWidth * segHeight, CV_32F, (float*)m_buffers[CLS_OR_DETECTION_OUTPUT_INDEX] + i * m_seg_channels * segWidth * segHeight);
        Mat matmulRes = (maskProposals * protos).t();//n*32 32*25600 A*B是以数学运算中矩阵相乘的方式实现的，要求A的列数等于B的行数时
        
        Mat masks = matmulRes.reshape(vOutput.size(), {segWidth, segHeight});//n*160*160
        std::vector<Mat> vMaskChannels;
        cv::split(masks, vMaskChannels);
        // cout<<"segHeight:"<<segHeight<<" segWidth:"<<segWidth<<" vMaskChannels"<<vMaskChannels.at(0).size()<<endl;
        Rect roi(int((float)padw / m_input_w * segWidth), int((float)padh / m_input_h * segHeight), int(segWidth - padw / 2), int(segHeight - padh / 2));
        for (int j = 0; j < vOutput.size(); ++j) {
            Mat dest, mask;
            cv::exp(-vMaskChannels[j], dest);//sigmoid
            dest = 1.0 / (1.0 + dest);//160*160
            dest = dest(roi);
            resize(dest, mask, cv::Size(img_w, img_h), INTER_NEAREST);
            //crop----截取box中的mask作为该box对应的mask
            Rect temp_rect = vOutput[j].box;
            mask = mask > m_mask_th;
            Mat addImg = Mat::zeros(Size(img_w, img_h), CV_8UC1);
            addImg(temp_rect) += mask(temp_rect);
            vOutput[j].boxMask = addImg;
        }
        vInstanceSegOutput.emplace_back(vOutput);
    }
    end = std::chrono::system_clock::now();
    // std::cout << "后处理时间：" << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
}

 
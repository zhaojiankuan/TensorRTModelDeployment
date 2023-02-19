//
// Created by Administrator on 2023/1/9.
//
#ifndef FACEFEATUREDETECTOR_REBUILD_FACE_INTERFACE_H
#define FACEFEATUREDETECTOR_REBUILD_FACE_INTERFACE_H
#include <tuple>

#include <NvInferRuntime.h>
#include <opencv2/opencv.hpp>
//#include "struct_data_type.h"
//#include "base_interface/ai_img_alg_base.h"
//#include "../algorithm_product/YoloFace.h"
#include "../algorithm_product/product.h"

#define checkRuntime(op) check_cuda_runtime((op),#op,__FILE__,__LINE__)

using Handle = void *;

bool check_cuda_runtime(cudaError_t code, const char *op, const char *file, int line);

// 初始化过程中,各个模型都会用到的通用步骤
int initCommon(struct ConfigBase &confSpecific, class AlgorithmBase *funcSpecific);

// 测试重构一下?
int inferEngine(struct productConfig &conf, struct productFunc &func, std::vector<cv::Mat> &images, int &res_num);
int initEngine(struct productConfig &conf, struct productFunc &func);
int inferEngine(struct productConfig &conf, struct productFunc &func, std::vector<cv::Mat> &matVector, struct productOutput out);
int inferEngine(struct productConfig &conf, struct productFunc &func, std::vector<cv::cuda::GpuMat> &matVector, struct productOutput out);

int releaseEngine(Handle engine);

/*
*   @brief                  获取人脸结果数量
*   @note                   每次返回人脸结果数量，人脸结果参数参考FASPFaceResult_t结构体
*   @param engine           输入需要获取人脸提取结果的人脸提取引擎handle
*   @param imgData          输入图片数据
*   @param imgWidth         输入图片宽
*   @param imgHeight        输入图片高
*   @param imgPixelFormat   输入图片像素排列类型
*   @param min_face_size    输入人脸框最小值，最小值推荐40
*   @param mode             输入人脸模式，0：单人脸模式，取图片中最大的人脸，1：多人脸模式，适用于通用场景， 默认为1
*   @param res_num          输出人脸结果个数
*   @return                 成功返回0；失败返回对应错误码
*/
int inferEngine(Handle engine, unsigned char *imgData, int imgWidth,
                int imgHeight, int min_face_size, int mode, int imgPixelFormat, int &res_num);

/*
*   @brief                  获取人脸特征提取结果
*   @param res_num          输入人脸特征提取结果数量，inferFrame接口返回值
*   @param res_ptr          输出人脸特征提取结果结构体指针
*   @return                 成功返回0；失败返回对应错误码
*/
int getResults(Handle engine, int res_num, struct FaceResult *res_ptr);

#endif //FACEFEATUREDETECTOR_REBUILD_FACE_INTERFACE_H
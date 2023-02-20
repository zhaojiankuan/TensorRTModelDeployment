#define HAVE_FACE_RETINA
#define HAVE_FACE_FEATURE

#include <iostream>
#include <iostream>
#include <chrono>

#include <string>
#include<sys/time.h>
#include<unistd.h>
#include <filesystem>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
//#include <dirent.h>

#include "interface/face_interface.h"
//#include "file_base.h"
#include "algorithm_factory/struct_data_type.h"
//#include "algorithm_product/product.h"
#include "utils/general.h"


int main(int argc, char *argv[]) {
    /*
    argc:参数个数
    *argv: 字符数组,记录输入的参数.可执行文件总在0号位,作为一个参数
    */
    // 判断参数个数, 若不为3,终止程序
    auto timer = new Timer();

    if (argc != 3) {
        std::cout << " the number of param is incorrect, must be 3, but now is " << argc << std::endl;
        std::cout << "param format is ./AiSdkDemo gpu_id img_dir_path" << std::endl;
        return -1;
    }

    // =====================================================================
    // 外接传入的配置文件,和使用过程中生成的各种路径等
    struct productConfig conf;
    // 加{},说明创建的对象为nullptr, 存储从动态库解析出来的算法函数和类
    struct productFunc func{};
    struct productOutput out;
    Handle engine;

//    conf.yoloConfig.onnxPath = "/mnt/e/GitHub/TensorRTModelDeployment/models/face_detect_v0.5_b17e5c7577192da3d3eb6b4bb850f8e_1out.onnx";
//    conf.yoloConfig.gpuId = int(strtol(argv[1], nullptr, 10));

    conf.detectConfig.onnxPath = "/mnt/e/GitHub/TensorRTModelDeployment/models/yolov5s.onnx";
    conf.detectConfig.gpuId = int(strtol(argv[1], nullptr, 10));

    int ret = initEngine(conf, func);
    std::cout << "init ok !" << std::endl;
    // =====================================================================

    if (ret != 0)
        return ret;

//    conf.score_sface_thresh = 0.9f;

    //创建输出文件夹
    std::string path1 = std::string(argv[2]) + "/";

    std::filesystem::path imgInputDir(path1);
    std::filesystem::path imgOutputDir(path1 + "output/");
    //检查文件夹路径是否合法, 检查输出文件夹路径是否存在,不存在则创建
    // 输入不是文件夹,或文件不存在抛出异常
    if (!std::filesystem::exists(imgInputDir) || !std::filesystem::is_directory(imgInputDir))
        return -1;
    //创建输出文件夹
    if (!std::filesystem::exists(imgOutputDir))
        std::filesystem::create_directories(imgOutputDir);

    std::vector<cv::Mat> matVector;
    // 获取该文件夹下所有图片绝对路径,存储在vector向量中
    getImageMatFromPath(imgInputDir, matVector);

    double inferTime = 0.0f;
    inferEngine(conf, func, matVector, out);
//    for (auto &imgPath: imgPaths) {
//        cv::Mat img = cv::imread(imgPath);
//        //记录当前时间
//        auto t1 = timer->curTimePoint();
//        //记录人脸推理结果
//        struct FaceResult *resPtr = nullptr;
//
//        int faceNum = 0, minFaceSize = 20, mode = 1;
//        cv::Mat image = cv::imread(imgPath);
//        // ??????
//        ret = inferEngine(conf, func, image, out);
//
////        ret = inferEngine(engine, img.data, img.cols, img.rows, minFaceSize, mode, FAS_PF_RGB24_B8G8R8, faceNum);
//        //把每张图片推理时间加到inferTime中 ms
//        inferTime += timer->timeCount(t1);
//        if (0 != ret) {
//            std::cout << "======== infer failed. use time = " << inferTime << "ms =========" << std::endl;
//            continue;
//        }
//
//        getResults(engine, faceNum, resPtr);

    //输出推理结果
//        for (int i = 0; i < faceNum; ++i) {
//            float x1 = resPtr[i].x1, y1 = resPtr[i].y1, x2 = resPtr[i].x2, y2 = resPtr[i].y2;
//            float confidence = resPtr[i].confidence;
//            float angleP = resPtr[i].angleP, angleR = resPtr[i].angleR, angleY = resPtr[i].angleY;
//            float landmark[10];
//            for (int j = 0; j < 10; ++j)
//                landmark[j] = resPtr[i].landmark[j];
//
//            int qualityType = resPtr[i].qualityType;
//            float qualityScore = resPtr[i].qualityScore;
//
//            std::cout << "=============face num " << i + 1 << "==============" << std::endl;
//            std::cout << "rect = (" << x1 << ", " << y1 << ", " << x2 << ", " << y2 << ")" << std::endl;
//            std::cout << "confidence = " << confidence << std::endl;
//            std::cout << "angle(p,r,y) = (" << angleP << ", " << angleR << ", " << angleY << ")" << std::endl;
//            std::cout << "quality_type = " << qualityType << std::endl;
//            std::cout << "quality_score = " << qualityScore << std::endl;
//
//            std::cout << "landmark = ";
//            for (float j: landmark) std::cout << j << ", ";
//
//            std::cout << "feature = ";
//            for (float j: resPtr[i].feature) std::cout << j << ", ";
//
//            std::cout << std::endl;
//
//            delete[] resPtr, resPtr = nullptr;
//        }
//    releaseEngine(engine);
    return 0;
}

//return 0;
//}

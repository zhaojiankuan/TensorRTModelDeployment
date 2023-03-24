//
// Created by Administrator on 2023/2/20.
//

#ifndef TENSORRTMODELDEPLOYMENT_YOLODETECT_H
#define TENSORRTMODELDEPLOYMENT_YOLODETECT_H

#include "../algorithm_factory/infer.h"
//#include "../algorithm_factory/struct_data_type.h"
#include "../utils/box_utils.h"
#include "../utils/general.h"
#include "product.h"

class YoloDetect : public Infer {
public:
    YoloDetect();
    ~YoloDetect() override;

    // 图片预处理
    int preProcess(ParamBase &param, cv::Mat &image, float *pinMemoryCurrentIn) override;
    // 图片后处理
    int postProcess(ParamBase &parm, float *pinMemoryOut, int singleOutputSize,
                    int outputNums, std::vector<std::vector<std::vector<float>>> &result) override;


    std::vector<std::vector<float>> decodeBox(int predictNums, int predictLength,
                                              float *pinOutput, int classNum, float scoreThresh, std::vector<float> d2i);

    std::vector<std::vector<std::vector<float>>> getCurResult();

private:
    // 各具体算法定义自己的输出数据结构,并在getCurResult方法中返回.
    std::vector<std::vector<std::vector<float>>> m_curResult;
};

#endif //TENSORRTMODELDEPLOYMENT_YOLODETECT_H

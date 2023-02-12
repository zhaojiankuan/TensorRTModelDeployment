//
// Created by Administrator on 2023/2/10.
//

#ifndef TENSORRT_PRO_YOLOFACE_H
#define TENSORRT_PRO_YOLOFACE_H

#endif //TENSORRT_PRO_YOLOFACE_H

#include "../algorithm_factory/factory.h"
#include "../algorithm_factory/struct_data_type.h"
#pragma once
class YoloFace : public AlgorithmFactory {
public:
    ~YoloFace() override;

    int initParam(void *param) override;
    int FAImgAlgInfer() override;

    YoloFaceConfig getArr() { return faceConfig; };
    YoloFaceConfig conf2;
private:
    YoloFaceConfig faceConfig;
};
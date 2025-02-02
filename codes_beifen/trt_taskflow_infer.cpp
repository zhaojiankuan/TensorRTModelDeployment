//
// Created by Administrator on 2023/4/1.
//
#include <cuda_runtime.h>
//onnx解释器头文件
#include <NvOnnxParser.h>
#include <fstream>
#include "../taskflow/taskflow.hpp"

#include "../utils/general.h"
#include "../utils/box_utils.h"
#include "../builder/trt_builder.h"

#include "infer.h"

class InferImpl : public Infer {
public:
    explicit InferImpl(std::vector<int> &memory);
    ~InferImpl() override;
    // 创建推理engine
    static bool getEngineContext(BaseParam &curParam);
    static std::vector<int> setBatchAndInferMemory(BaseParam &curParam);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////
//    preProcess,postProcess空实现,具体实现由实际继承Infer.h的应用完成
    int preProcess(BaseParam &param, cv::Mat &image, float *pinMemoryCurrentIn) override {};

    int postProcess(BaseParam &param, float *pinMemoryCurrentOut, int singleOutputSize, int outputNums, batchBoxesType &result) override {};

//    具体应用调用commit方法,推理数据传入队列, 直接返回future对象. 数据依次经过trtPre,trtInfer,trtPost三个线程,结果通过future.get()获得
    std::shared_future<batchBoxesType> commit(const InputData *data) override;

//    将待推理数据写入队列1, 会调用上述由具体应用实现的preProcess
    void trtPre(BaseParam &curParam, Infer *curFunc);
//    从队列1取数据进行推理,然后将推理结果写入队列2
    void trtInfer(BaseParam &curParam);
//    从队列2取数据,进行后处理, 会调用上述由具体应用实现的postProcess
    void trtPost(BaseParam &curParam, Infer *curFunc);

    bool startThread(BaseParam &curParam, Infer &curFunc);

private:
//   lock1用于预处理写入+推理时取出
    std::mutex lock1;
//   lock2用于推理后结果写入+后处理取出
    std::mutex lock2;
    std::condition_variable cv_;
    float *gpuMemoryIn0 = nullptr, *gpuMemoryIn1 = nullptr, *pinMemoryIn = nullptr;
    float *gpuMemoryOut0 = nullptr, *gpuMemoryOut1 = nullptr, *pinMemoryOut = nullptr;
    float *gpuIn[2]{}, *gpuOut[2]{};
    std::vector<int> memory;
    // 读取从路径读入的图片矩阵
    cv::Mat mat;
    std::queue<Job> qPreJobs;
// 存储每个batch的推理结果,统一后处理
    std::queue<Out> qPostJobs;
    futureJob fJob;
    std::queue<futureJob> qfJobs;
    // 记录传入的图片数量
    std::queue<int> qfJobLength;
    std::atomic<bool> preFinish{false}, inferFinish{false}, workRunning{true};
    std::shared_ptr<std::thread> preThread, inferThread, postThread;
    //创建cuda任务流,对应上述三个处理线程
    cudaStream_t preStream{}, inferStream{}, postStream{};
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Timer timer = Timer();

//使用所有加速算法的初始化部分: 初始化参数,构建engine, 反序列化制作engine
bool InferImpl::getEngineContext(BaseParam &curParam) {
    std::vector<unsigned char> engine;
    try {
        engine = TRT::getEngine(curParam);
    } catch (std::string &error) {
//        捕获未知的异常情况
        logError("load engine failure: %s", error.c_str());
        return false;
    }
    if (engine.empty()) {
        logError("load engine failure");
        return false;
    }
//   也可以直接从字符串提取名字 curParam.enginePath.substr(curParam.enginePath.find_last_of("/"),-1)
    logSuccess("load engine success: %s", std::filesystem::path(curParam.enginePath).filename().c_str());

    // 创建engine并获得执行上下文context =======================================================================================
    TRTLogger logger;
//   在trt8.6版本中,使用自动释放会出错
//    auto runtime = ptrFree(nvinfer1::createInferRuntime(logger));
    auto runtime = nvinfer1::createInferRuntime(logger);

    try {
        curParam.engine = ptrFree(runtime->deserializeCudaEngine(engine.data(), engine.size()));
    } catch (std::string &error) {
//        捕获未知的异常情况
        logSuccess("deserialize cuda engine failure: %s", error.c_str());
        return false;
    }
    if (nullptr == curParam.engine) {
        logError("deserialize cuda engine failure");
        return false;
    }
    logSuccess("deserialize cuda engine success");

//    限定推理引擎只有1个输入和1个输出,不处理多节点模型.是因为有1个是输入.剩下的才是输出,所以要-1
    if (2 != curParam.engine->getNbIOTensors()) {
        logError("expect engine's node num is 2, 1 input and 1 output, but now node num is %d", curParam.engine->getNbIOTensors() - 1);
        return false;
    }

    curParam.context = ptrFree(curParam.engine->createExecutionContext());
    logSuccess("create context success");
    return true;
}

std::shared_future<batchBoxesType> InferImpl::commit(const InputData *data) {
//std::shared_future <batchBoxesType> InferImpl::commit(const std::vector <cv::Mat> &images) {
    // 将传入的多张或一张图片,一次性传入队列总
    unsigned long len = !data->mats.empty() ? data->mats.size() : data->gpuMats.size();

    fJob.mats = data->mats;
    fJob.gpuMats = data->gpuMats;
//    两种方法都可以实现初始化,make_shared更好?
    fJob.batchResult = std::make_shared<std::promise<batchBoxesType>>();
//    fJob.batchResult.reset(new std::promise<batchBoxesType>());

    // 创建share_future变量,一次性取回传入的所有图片结果, 不能直接返回xx.get_future(),会报错
    std::shared_future<batchBoxesType> future = fJob.batchResult->get_future();
    {
        std::lock_guard<std::mutex> l1(lock1);
        qfJobLength.emplace(len);
        qfJobs.emplace(std::move(fJob));
    }
    // 通知图片预处理线程,图片已就位.
    cv_.notify_all();

    return future;
}

void InferImpl::trtPre(BaseParam &curParam, Infer *curFunc) {
    // 记录预处理总耗时
    double totalTime;
    std::chrono::system_clock::time_point preTime;
//    auto t = timer.curTimePoint();
//    计算1个batchSize的数据所需的空间大小
    unsigned long mallocSize = this->memory[0] * sizeof(float);
    // count统计推理图片数量,最后一次,可能小于batchSize.imgNums统计预处理图片数量,index是gpuIn的索引,两个显存轮换使用
    int countPre = 0, index = 0, inputSize = this->memory[0] / curParam.batchSize;
    Job job;

    while (workRunning) {
        {
            std::unique_lock<std::mutex> l1(lock1);
            // 队列不为空, 退出等待,. 当标志位workRunning为False时,说明要停止线程运行,也退出等待
            cv_.wait(l1, [&]() { return !qfJobs.empty() || !workRunning; });
//            判断退出等待原因是否是要停止线程
            if (!workRunning) break;
            fJob = qfJobs.front();
            qfJobs.pop();
        }
        // 默认推理的数量为batchSize, 只有最后一次才可能小于batchSize
        job.inferNum = curParam.batchSize;
//        记录待推理的最后一个元素地址
        auto lastElement = &fJob.mats.back();

        // todo 暂时,先在内存中进行图片预处理. gpuMat以后写cuda核函数处理
        for (auto &curMat: fJob.mats) {
            // 记录前处理耗时
            preTime = timer.curTimePoint();
//            调用具体算法自身实现的预处理逻辑
            curFunc->preProcess(curParam, curMat, pinMemoryIn + countPre * inputSize);
            // 将当前正在处理图片的变换参数加入该batch的变换vector中
            job.d2is.push_back({curParam.d2i[0], curParam.d2i[1], curParam.d2i[2], curParam.d2i[3], curParam.d2i[4], curParam.d2i[5]});

            countPre += 1;
            // 不是最后一个元素且数量小于batchSize,继续循环,向pinMemoryIn写入预处理后数据
            if (&curMat != lastElement && countPre < curParam.batchSize) continue;
            // 若是最后一个元素,记录当前需要推理图片数量(可能小于一个batchSize)
            if (&curMat == lastElement) job.inferNum = countPre;

            totalTime += timer.timeCountS(preTime);
            //若全部在gpu上操作,不需要这句复制
            checkRuntime(cudaMemcpyAsync(gpuIn[index], pinMemoryIn, mallocSize, cudaMemcpyHostToDevice, preStream));
            job.inputTensor = gpuIn[index];

            countPre = 0;
//            将内存地址索引指向另一块内存
            index = index >= 1 ? 0 : index + 1;

            {
                std::unique_lock<std::mutex> l1(lock1);
                // false,继续等待. true,不等待,跳出wait. 一旦进入wait,解锁. 退出wait,又加锁 最多2个batch
                cv_.wait(l1, [&]() { return qPreJobs.size() < 2; });
                // 将一个batch待推理数据的显存空间指针保存
                qPreJobs.push(job);
            }

            // 流同步,在通知队列可使用前,确保待推理数据已复制到gpu上,保证在推理时取出就能用
            checkRuntime(cudaStreamSynchronize(preStream));
            cv_.notify_all();
            // 清空保存仿射变换参数,只保留当前推理batch的参数
            std::vector<std::vector<float >>().swap(job.d2is);
        }
    }
    // 结束预处理线程,释放资源
    preFinish = true;
    // 唤醒trt线程,告知预处理线程已结束
    cv_.notify_all();
//    printf("pre   use time: %.2f ms\n", totalTime);
    logInfo("pre   use time: %.3f s", totalTime);
}

// 适用于模型推理的通用trt流程
void InferImpl::trtInfer(BaseParam &curParam) {
    // 记录推理耗时
    double totalTime;
//    auto t = timer.curTimePoint();
    int index2 = 0;
//    engine输入输出节点名字, 是把model编译为onnx文件时,在onnx中手动写的输入输出节点名
    const char *inferInputName = curParam.inputName.c_str();
    const char *inferOutputName = curParam.outputName.c_str();

    Job job;
    Out outTrt;

    while (true) {
        {
            std::unique_lock<std::mutex> l1(lock1);
            // 队列不为空, 就说明推理空间图片已准备好,退出等待,继续推理. 当图片都处理完,并且队列为空,要退出等待,因为此时推理工作已完成
            cv_.wait(l1, [&]() { return !qPreJobs.empty() || (preFinish && qPreJobs.empty()); });
//            若图片都处理完且队列空,说明推理结束,直接退出线程
            if (preFinish && qPreJobs.empty()) break;
            job = qPreJobs.front();
            qPreJobs.pop();
        }
        // 消费掉一个元素,通知队列跳出等待,向qJob继续写入一个batch的待推理数据
        cv_.notify_all();

        auto qT1 = timer.curTimePoint();
        // 指定tensor的输入输出地址,然后进行推理
        curParam.context->setTensorAddress(inferInputName, job.inputTensor);
        curParam.context->setTensorAddress(inferOutputName, gpuOut[index2]);
        curParam.context->enqueueV3(inferStream);

        outTrt.inferOut = gpuOut[index2];
        outTrt.d2is = job.d2is;
        outTrt.inferNum = job.inferNum;

        index2 = index2 >= 1 ? 0 : index2 + 1;

        cudaStreamSynchronize(inferStream);
        totalTime += timer.timeCountS(qT1);

        // 流同步后,获取该batchSize推理结果
        {
            std::unique_lock<std::mutex> l2(lock2);
            // false, 表示继续等待. true, 表示不等待,gpuOut内只有两块空间,因此队列长度只能限定为2
            cv_.wait(l2, [&]() { return qPostJobs.size() < 2; });
            qPostJobs.emplace(outTrt);
        }
        cv_.notify_all();
    }
    // 在post后处理线程中判断,所有推理流程是否结束,然后决定是否结束后处理线程
    inferFinish = true;
    //告知post后处理线程,推理线程已结束
    cv_.notify_all();
//    printf("infer use time: %.2f ms\n", totalTime);
    logInfo("infer use time: %.3f s", totalTime);
}

void InferImpl::trtPost(BaseParam &curParam, Infer *curFunc) {
    // 记录后处理耗时
    double totalTime;
//    auto t = timer.curTimePoint();
//    将推理后数据从从显存拷贝到内存中,计算所需内存大小,ps:其实与占用显存大小一致
    unsigned long mallocSize = this->memory[1] * sizeof(float), singleOutputSize = this->memory[1] / curParam.batchSize;
//    batchBox收集每个batchSize后处理结果,然后汇总到batchBoxes中
    batchBoxesType batchBoxes, batchBox;

//    传入图片总数, 已处理图片数量
    int totalNum = 0, countPost = 0;
    bool flag = true;
    Out outPost;

    while (true) {
        {
            std::unique_lock<std::mutex> l2(lock2);
            // 队列不为空, 就说明图片已推理好,退出等待,进行后处理. 推理结束,并且队列为空,退出等待,因为此时推理工作已完成
            cv_.wait(l2, [&]() { return !qPostJobs.empty() || (inferFinish && qPostJobs.empty()); });
            // 退出推理线程
            if (inferFinish && qPostJobs.empty()) break;
            outPost = qPostJobs.front();
            qPostJobs.pop();
            cv_.notify_all();
//           每次当python传入数据时,totalNum值才会取出一次,获得当前传入图片数量
            if (flag) {
                totalNum = qfJobLength.front();
                qfJobLength.pop();
                flag = false;
            }
        }
        // todo 将engine推理好的结果从gpu转移到内存中处理, 更高效的方式是在在gpu中用cuda核函数处理,but,以后再扩展吧
        cudaMemcpy(pinMemoryOut, outPost.inferOut, mallocSize, cudaMemcpyDeviceToHost);
        // 取回数据的仿射变换量,用于还原预测量在原图尺寸上的位置
        curParam.d2is = outPost.d2is;

        auto qT1 = timer.curTimePoint();
//        记录当前后处理图片数量, 若是单张图片,这个记录没啥用. 若是传入多个batchSize的图片,countPost会起到标识作用
        countPost += outPost.inferNum;
//        调用具体算法自身实现的后处理逻辑
        curFunc->postProcess(curParam, pinMemoryOut, singleOutputSize, outPost.inferNum, batchBox);
        //将每次后处理结果合并到输出vector中
        batchBoxes.insert(batchBoxes.end(), batchBox.begin(), batchBox.end());
        batchBox.clear();
        // 当commit中传入图片处理完时,通过set_value返回所有图片结果. 重新计数, 并返回下一次要输出推理结果的图片数量
        if (totalNum <= countPost) {
//            fJob.batchResult = std::make_shared<std::promise<batchBoxesType>>();
            // 输出解码后的结果,在commit中接收
            fJob.batchResult->set_value(batchBoxes);
            countPost = 0;
            flag = true;
            batchBoxes.clear();
        }
        totalTime += timer.timeCountS(qT1);
    }
//    printf("post  use time: %.2f ms\n", totalTime);
    logInfo("post  use time: %.3f s", totalTime);

}

std::vector<int> InferImpl::setBatchAndInferMemory(BaseParam &curParam) {
    //计算输入tensor所占存储空间大小,设置指定的动态batch的大小,之后再重新指定输入tensor形状
    auto inputShape = curParam.engine->getTensorShape(curParam.inputName.c_str());
    inputShape.d[0] = curParam.batchSize;
    curParam.context->setInputShape(curParam.inputName.c_str(), inputShape);
    //batchSize * c * h * w
    int inputSize = curParam.batchSize * inputShape.d[1] * inputShape.d[2] * inputShape.d[3];

    // 获得输出tensor形状,计算输出所占存储空间
    auto outputShape = curParam.engine->getTensorShape(curParam.outputName.c_str());
    // 记录这两个输出维度数量,在后处理时会用到
    curParam.predictNums = outputShape.d[1];
    curParam.predictLength = outputShape.d[2];
    // 计算推理结果所占内存空间大小
    int outputSize = curParam.batchSize * outputShape.d[1] * outputShape.d[2];

    // 将batchSize个输入输出所占空间大小返回
    std::vector<int> memory = {inputSize, outputSize};
    return memory;
}

bool InferImpl::startThread(BaseParam &curParam, Infer &curFunc) {
    try {
        preThread = std::make_shared<std::thread>(&InferImpl::trtPre, this, std::ref(curParam), &curFunc);
        inferThread = std::make_shared<std::thread>(&InferImpl::trtInfer, this, std::ref(curParam));
        postThread = std::make_shared<std::thread>(&InferImpl::trtPost, this, std::ref(curParam), &curFunc);
    } catch (std::string &error) {
        logError("thread start failure: %s !", error.c_str());
        return false;
    }

    logSuccess("thread start success !");
    return true;
}

InferImpl::InferImpl(std::vector<int> &memory) {
    this->memory = memory;

    cudaStream_t initStream;
    cudaStreamCreate(&initStream);

    cudaStreamCreate(&preStream);
    cudaStreamCreate(&inferStream);
    cudaStreamCreate(&postStream);

    unsigned long InMallocSize = memory[0] * sizeof(float);
    unsigned long OutMallocSize = memory[1] * sizeof(float);

    checkRuntime(cudaMallocAsync(&gpuMemoryIn0, InMallocSize, initStream));
    checkRuntime(cudaMallocAsync(&gpuMemoryIn1, InMallocSize, initStream));

    checkRuntime(cudaMallocAsync(&gpuMemoryOut0, OutMallocSize, initStream));
    checkRuntime(cudaMallocAsync(&gpuMemoryOut1, OutMallocSize, initStream));

    checkRuntime(cudaMallocHost(&pinMemoryIn, InMallocSize));
    checkRuntime(cudaMallocHost(&pinMemoryOut, OutMallocSize));

    gpuIn[0] = gpuMemoryIn0;
    gpuIn[1] = gpuMemoryIn1;
    gpuOut[0] = gpuMemoryOut0;
    gpuOut[1] = gpuMemoryOut1;

    checkRuntime(cudaStreamDestroy(initStream));
}

InferImpl::~InferImpl() {
    logInfo("start executing destructor ...");
    if (workRunning) {
        workRunning = false;
        cv_.notify_all();
    }

    if (preThread->joinable()) preThread->join();
    if (inferThread->joinable()) inferThread->join();
    if (postThread->joinable()) postThread->join();

    checkRuntime(cudaFree(gpuMemoryIn0));
    checkRuntime(cudaFree(gpuMemoryIn1));
    checkRuntime(cudaFree(gpuMemoryOut0));
    checkRuntime(cudaFree(gpuMemoryOut1));

    checkRuntime(cudaFreeHost(pinMemoryIn));
    checkRuntime(cudaFreeHost(pinMemoryOut));

    checkRuntime(cudaStreamDestroy(preStream));
    checkRuntime(cudaStreamDestroy(inferStream));
    checkRuntime(cudaStreamDestroy(postStream));
//    printf("析构函数\n");
}

std::shared_ptr<Infer> createInfer(BaseParam &curParam, Infer &curFunc) {
//    如果创建引擎不成功就reset
    if (!InferImpl::getEngineContext(curParam)) {
//        logError("getEngineContext Fail");
        return nullptr;
    }
    std::vector<int> memory;
    try {
        memory = InferImpl::setBatchAndInferMemory(curParam);
    } catch (std::string &error) {
        logError("setBatchAndInferMemory failure: %s !", error.c_str());
        return nullptr;
    }
    // 实例化一个推理器的实现类（inferImpl），以指针形式返回
    std::shared_ptr<InferImpl> instance(new InferImpl(memory));

    //若实例化失败 或 若线程启动失败,也返回空实例. 所有的错误信息都在函数内部打印
    if (!instance || !instance->startThread(curParam, curFunc)) {
//        logError("InferImpl instance Fail");
        instance.reset();
        return nullptr;
    }

    return instance;
}
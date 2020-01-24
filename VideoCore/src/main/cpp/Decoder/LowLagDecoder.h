//
// Created by Constantin on 29.05.2017.
//

#ifndef LOW_LAG_DECODER
#define LOW_LAG_DECODER

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <android/log.h>
#include <jni.h>
#include <iostream>
#include <thread>
#include <atomic>

#include "../NALU/NALU.hpp"
#include "../Helper/TimeHelper.hpp"


class LowLagDecoder {
private:
    struct Decoder{
        bool configured= false;
        bool SW= false;
        AMediaCodec *codec= nullptr;
        ANativeWindow* window= nullptr;
        AMediaFormat *format=nullptr;
    };
public:
    struct DecodingInfo{
        std::chrono::steady_clock::time_point lastCalculation=std::chrono::steady_clock::now();
        long nNALU=0;
        long nNALUSFeeded=0;
        float currentFPS=0;
        float currentKiloBitsPerSecond=0;
        float avgParsingTime_ms=0;
        float avgWaitForInputBTime_ms=0;
        float avgDecodingTime_ms=0;
    };
    typedef std::function<void(DecodingInfo&)> DECODING_INFO_CHANGED_CALLBACK;
    typedef std::function<void(int,int)> DECODER_RATIO_CHANGED;
public:
    LowLagDecoder(ANativeWindow* window,int checkOutputThreadCpuPrio);
    void registerOnDecoderRatioChangedCallback(DECODER_RATIO_CHANGED decoderRatioChangedC);
    void registerOnDecodingInfoChangedCallback(DECODING_INFO_CHANGED_CALLBACK decodingInfoChangedCallback);
    void interpretNALU(const NALU& nalu);
    void waitForShutdownAndDelete();
private:
    void configureStartDecoder(const NALU& nalu);
    void feedDecoder(const NALU& nalu,bool justEOS);
    void checkOutputLoop();
    void printAvgLog();
    void closeInputPipe();
    int mWidth,mHeight;
    std::thread* mCheckOutputThread= nullptr;
    const int mCheckOutputThreadCPUPriority;
    Decoder decoder;
    DecodingInfo decodingInfo;
    uint8_t CSDO[NALU_MAXLEN],CSD1[NALU_MAXLEN];
    int CSD0Length=0,CSD1Length=0;
    bool inputPipeClosed=false;
    std::mutex mMutexInputPipe;
    DECODER_RATIO_CHANGED onDecoderRatioChangedCallback= nullptr;
    DECODING_INFO_CHANGED_CALLBACK onDecodingInfoChangedCallback= nullptr;
    std::chrono::steady_clock::time_point lastLog=std::chrono::steady_clock::now();
    RelativeCalculator nDecodedFrames;
    RelativeCalculator nNALUBytesFed;
    AvgCalculator parsingTime_us;
    AvgCalculator waitForInputB_us;
    AvgCalculator decodingTime_us;
};

#endif //LOW_LAG_DECODER

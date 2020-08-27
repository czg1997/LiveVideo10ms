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
#include <TimeHelper.hpp>
#include <SharedPreferences.hpp>
#include "../NALU/KeyFrameFinder.hpp"

struct DecodingInfo{
    std::chrono::steady_clock::time_point lastCalculation=std::chrono::steady_clock::now();
    long nNALU=0;
    long nNALUSFeeded=0;
    float currentFPS=0;
    float currentKiloBitsPerSecond=0;
    float avgParsingTime_ms=0;
    float avgWaitForInputBTime_ms=0;
    float avgDecodingTime_ms=0;
    bool operator==(const DecodingInfo& d2)const{
        return nNALU==d2.nNALU && nNALUSFeeded==d2.nNALUSFeeded && currentFPS==d2.currentFPS &&
               currentKiloBitsPerSecond==d2.currentKiloBitsPerSecond && avgParsingTime_ms==d2.avgParsingTime_ms &&
               avgWaitForInputBTime_ms==d2.avgWaitForInputBTime_ms && avgDecodingTime_ms==d2.avgDecodingTime_ms;
    }
    bool operator !=(const DecodingInfo& d2)const{
        return !(*this==d2);
    }
};
struct VideoRatio{
    int width=0;
    int height=0;
    bool operator==(const VideoRatio& b)const{
        return width==b.width && height==b.height;
    }
    bool operator !=(const VideoRatio& b)const{
        return !(*this==b);
    }
};

//Handles decoding of .h264 video
class LowLagDecoder {
private:
    struct Decoder{
        bool configured= false;
        AMediaCodec *codec= nullptr;
        ANativeWindow* window= nullptr;
    };
public:
    //Make sure to do no heavy lifting on this callback, since it is called from the low-latency mCheckOutputThread thread (best to copy values and leave processing to another thread)
    //The decoding info callback is called every DECODING_INFO_RECALCULATION_INTERVAL_MS
    typedef std::function<void(const DecodingInfo)> DECODING_INFO_CHANGED_CALLBACK;
    //The decoder ratio callback is called every time the output format changes
    typedef std::function<void(const VideoRatio)> DECODER_RATIO_CHANGED;
public:
    //We cannot initialize the Decoder until we have SPS and PPS data -
    //when streaming this data will be available at some point in future
    //Therefore we don't allocate the MediaCodec resources here
    LowLagDecoder(JNIEnv* env);
    // This call acquires or releases the output surface
    // After acquiring the surface, the decoder will be started as soon as enough configuration data was passed to it
    // When releasing the surface, the decoder will be stopped if running and any resources will be freed
    // After releasing the surface it is safe for the android os to delete it
    void setOutputSurface(JNIEnv* env,jobject surface,SharedPreferences& videoSettings);
    //register the specified callbacks. Only one can be registered at a time
    void registerOnDecoderRatioChangedCallback(DECODER_RATIO_CHANGED decoderRatioChangedC);
    void registerOnDecodingInfoChangedCallback(DECODING_INFO_CHANGED_CALLBACK decodingInfoChangedCallback);
    //If the decoder has been configured, feed NALU. Else search for configuration data and
    //configure as soon as possible
    // If the input pipe was closed, only buffer key frames
    void interpretNALU(const NALU& nalu);
private:
    //Initialize decoder with provided SPS/PPS data.
    //Set Decoder.configured to true on success
    void configureStartDecoder(const NALU& sps,const NALU& pps);
    //Wait for input buffer to become available before feeding NALU
    void feedDecoder(const NALU& nalu);
    //Runs until EOS arrives at output buffer or decoder is stopped
    void checkOutputLoop();
    //Debug log
    void printAvgLog();
    void resetStatistics();
    std::unique_ptr<std::thread> mCheckOutputThread= nullptr;
    bool USE_SW_DECODER_INSTEAD=false;
    //Holds the AMediaCodec instance, as well as the state (configured or not configured)
    Decoder decoder;
    DecodingInfo decodingInfo;
    bool inputPipeClosed=false;
    std::mutex mMutexInputPipe;
    DECODER_RATIO_CHANGED onDecoderRatioChangedCallback= nullptr;
    DECODING_INFO_CHANGED_CALLBACK onDecodingInfoChangedCallback= nullptr;
    // So we can temporarily attach the output thread to the vm and make ndk calls
    JavaVM* javaVm=nullptr;
    std::chrono::steady_clock::time_point lastLog=std::chrono::steady_clock::now();
    RelativeCalculator nDecodedFrames;
    RelativeCalculator nNALUBytesFed;
    AvgCalculator parsingTime;
    AvgCalculator waitForInputB;
    AvgCalculator decodingTime;
    //Every n ms re-calculate the Decoding info
    static const constexpr auto DECODING_INFO_RECALCULATION_INTERVAL=std::chrono::milliseconds(1000);
    static constexpr const bool PRINT_DEBUG_INFO=true;
    static constexpr auto TIME_BETWEEN_LOGS=std::chrono::seconds(5);
    static constexpr int64_t BUFFER_TIMEOUT_US=35*1000; //40ms (a little bit more than 32 ms (==30 fps))
private:
    static constexpr uint8_t SPS_X264[31]{
            0,0,0,1,103,66,192,40,217,0,120,2,39,229,192,90,128,128,128,160,0,0,125,32,0,29,76,17,227,6,73
    };
    static constexpr uint8_t SPS_X264_NO_VUI[15]{
            0,0,0,1,103,66,192,40,217,0,120,2,39,229,64
    };
    KeyFrameFinder mKeyFrameFinder;
};

#endif //LOW_LAG_DECODER

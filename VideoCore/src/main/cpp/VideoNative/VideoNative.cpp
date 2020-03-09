
#include "VideoNative.h"
#include "../Helper/CPUPriority.hpp"
#include "../IDV.hpp"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>

constexpr auto TAG="VideoNative";
#define MLOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

constexpr auto CPU_PRIORITY_UDPRECEIVER_VIDEO=(-16);  //needs low latency and does not use the cpu that much
constexpr auto CPU_PRIORITY_DECODER_OUTPUT (-16);     //needs low latency and does not use the cpu that much


VideoNative::VideoNative(JNIEnv* env,jobject context,const char* DIR) :
    mParser{std::bind(&VideoNative::onNewNALU, this, std::placeholders::_1)},
    mSettingsN(env,context,"pref_video",true),
    GROUND_RECORDING_DIRECTORY(DIR){
}

//Not yet parsed bit stream (e.g. raw h264 or rtp data)
void VideoNative::onNewVideoData(const uint8_t* data,const std::size_t data_length,const bool isRTPData,const int maxFPS=-1){
    mParser.setLimitFPS(maxFPS);
    //MLOGD("onNewVideoData %d",data_length);
    if(isRTPData){
        mParser.parse_rtp_h264_stream(data,data_length);
    }else{
        mParser.parse_raw_h264_stream(data,data_length);
    }
}

void VideoNative::onNewNALU(const NALU& nalu){
    //MLOGD("VideoNative::onNewNALU %d %s",(int)nalu.data_length,nalu.get_nal_name().c_str());
    //nalu.debugX();
    if(mLowLagDecoder){
        mLowLagDecoder->interpretNALU(nalu);
    }
    //same as get!=nullptr
    if(mGroundRecorder){
        mGroundRecorder->writeData(nalu.data,nalu.data_length);
    }
    if(mMP4GroundRecorder){
        mMP4GroundRecorder->writeData(nalu);
    }
}


void VideoNative::addConsumers(JNIEnv* env,jobject surface) {
    //reset the parser so the statistics start again from 0
    mParser.reset();
    //set the jni object for settings
    mSettingsN.replaceJNI(env);
    //add decoder if surface!=nullptr
    const bool VS_USE_SW_DECODER=mSettingsN.getBoolean(IDV::VS_USE_SW_DECODER);
    if(surface!= nullptr){
        window=ANativeWindow_fromSurface(env,surface);
        mLowLagDecoder=std::make_unique<LowLagDecoder>(window,CPU_PRIORITY_DECODER_OUTPUT,VS_USE_SW_DECODER);
        mLowLagDecoder->registerOnDecoderRatioChangedCallback([this](const VideoRatio ratio) {
            const bool changed=!(ratio==this->latestVideoRatio);
            this->latestVideoRatio=ratio;
            latestVideoRatioChanged=changed;
        });
        mLowLagDecoder->registerOnDecodingInfoChangedCallback([this](const DecodingInfo info) {
            const bool changed=!(info==this->latestDecodingInfo);
            this->latestDecodingInfo=info;
            latestDecodingInfoChanged=changed;
        });
    }
    //Add Ground recorder if enabled
    const bool VS_GroundRecording=mSettingsN.getBoolean(IDV::VS_GROUND_RECORDING);
    const auto VS_SOURCE= static_cast<SOURCE_TYPE_OPTIONS>(mSettingsN.getInt(IDV::VS_SOURCE));
    if(VS_GroundRecording && VS_SOURCE!=FILE && VS_SOURCE != ASSETS){
    //if(false){
        const std::string groundRecordingFlename=GroundRecorderRAW::findUnusedFilename(GROUND_RECORDING_DIRECTORY,"h264");
        //MLOGD("Filename%s",groundRecordingFlename.c_str());
         mGroundRecorder=std::make_unique<GroundRecorderRAW>(groundRecordingFlename);
    }
    //mMP4GroundRecorder=new GroundRecorderMP4(GROUND_RECORDING_DIRECTORY);
}

void VideoNative::removeConsumers(){
    if(mLowLagDecoder){
        mLowLagDecoder->waitForShutdownAndDelete();
        mLowLagDecoder.reset();
    }
    mGroundRecorder.reset();
    mMP4GroundRecorder.reset();
    if(window!=nullptr){
        //Don't forget to release the window, does not matter if the decoder has been created or not
        ANativeWindow_release(window);
        window=nullptr;
    }
}

void VideoNative::startReceiver(JNIEnv *env, AAssetManager *assetManager) {
    mSettingsN.replaceJNI(env);
    const auto VS_SOURCE= static_cast<SOURCE_TYPE_OPTIONS>(mSettingsN.getInt(IDV::VS_SOURCE));
    const int VS_FILE_ONLY_LIMIT_FPS=mSettingsN.getInt(IDV::VS_FILE_ONLY_LIMIT_FPS,60);

    //TODO use url instead of all these settings
    //If url starts with udp: raw h264 over udp
    //If url starts with rtp: rtp h264 over udp
    //If url starts with asset: play file from android assets folder
    //If url starts with file: play file from android local storage
    //If url starts with rtsp: play rtsp via ffmpeg

    //MLOGD("VS_SOURCE: %d",VS_SOURCE);
    switch (VS_SOURCE){
        case UDP:{
            const int VS_PORT=mSettingsN.getInt(IDV::VS_PORT);
            const bool useRTP= mSettingsN.getInt(IDV::VS_PROTOCOL) ==0;
            mVideoReceiver=std::make_unique<UDPReceiver>(VS_PORT,"VideoPlayer VideoReceiver",CPU_PRIORITY_UDPRECEIVER_VIDEO,[this,useRTP](const uint8_t* data,size_t data_length) {
                onNewVideoData(data,data_length,useRTP,-1);
            },WANTED_UDP_RCVBUF_SIZE);
            mVideoReceiver->startReceiving();
        }break;
        case FILE:{
            const std::string filename=mSettingsN.getString(IDV::VS_PLAYBACK_FILENAME);
            mFileReceiver=std::make_unique<FileReader>(filename,[this,VS_FILE_ONLY_LIMIT_FPS](const uint8_t* data,size_t data_length) {
                onNewVideoData(data,data_length,false,VS_FILE_ONLY_LIMIT_FPS);
            },1024);
            mFileReceiver->startReading();
        }break;
        case ASSETS:{
            const std::string filename=mSettingsN.getString(IDV::VS_ASSETS_FILENAME_TEST_ONLY,"testVideo.h264");
            mFileReceiver=std::make_unique<FileReader>(assetManager,filename,[this,VS_FILE_ONLY_LIMIT_FPS](const uint8_t* data,size_t data_length) {
                onNewVideoData(data,data_length,false,VS_FILE_ONLY_LIMIT_FPS);
            },1024);
            mFileReceiver->startReading();
        }break;
        case VIA_FFMPEG_URL:{
            MLOGD("Started with SOURCE=TEST360");
            const std::string url=mSettingsN.getString(IDV::VS_FFMPEG_URL);
            //const std::string url="file:/storage/emulated/0/DCIM/FPV_VR/capture1.h264";
            //const std::string url="file:/storage/emulated/0/DCIM/FPV_VR/360_test.h264";
            //const std::string url="file:/storage/emulated/0/DCIM/FPV_VR/360.h264";
            //const std::string url="file:/storage/emulated/0/DCIM/FPV_VR/test.mp4";
            //MLOGD("url:%s",url.c_str());
            mFFMpegVideoReceiver=std::make_unique<FFMpegVideoReceiver>(url,0,[this](uint8_t* data,int data_length) {
                onNewVideoData(data,(size_t)data_length,false,-1);
            },[this](const NALU& nalu) {
                onNewNALU(nalu);
            });
            mFFMpegVideoReceiver->start_playing();
        }break;
        case EXTERNAL:{
            //Data is being received somewhere else and passed trough-init nothing.
            MLOGD("Started with SOURCE=EXTERNAL");
        }break;
    }
}

void VideoNative::stopReceiver() {
    if(mVideoReceiver){
        mVideoReceiver->stopReceiving();
       mVideoReceiver.reset();
    }
    if(mFileReceiver){
        mFileReceiver->stopReading();
        mFileReceiver.reset();
    }
    if(mFFMpegVideoReceiver){
        mFFMpegVideoReceiver->shutdown_callback();
        mFFMpegVideoReceiver->stop_playing();
        mFFMpegVideoReceiver.reset();
    }
}

std::string VideoNative::getInfoString(){
    std::ostringstream ostringstream1;
    if(mVideoReceiver){
        ostringstream1 << "Listening for video on port " << mVideoReceiver->getPort();
        ostringstream1 << "\nReceived: " << mVideoReceiver->getNReceivedBytes() << "B"
                       << " | parsed frames: "
                       << mParser.nParsedNALUs<<" | key frames: "<<mParser.nParsedKeyFrames;
    }else{
        ostringstream1<<"Not receiving from udp";
    }
    return ostringstream1.str();
}

//----------------------------------------------------JAVA bindings---------------------------------------------------------------
#define JNI_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_constantin_video_core_VideoNative_VideoNative_##method_name

inline jlong jptr(VideoNative *videoPlayerN) {
    return reinterpret_cast<intptr_t>(videoPlayerN);
}
inline VideoNative *native(jlong ptr) {
    return reinterpret_cast<VideoNative *>(ptr);
}


extern "C"{

JNI_METHOD(jlong, initialize)
(JNIEnv * env,jclass jclass1,jobject context,jstring groundRecordingDirectory) {
    const char *str = env->GetStringUTFChars(groundRecordingDirectory, nullptr);
    auto* p=new VideoNative(env,context,str);
    env->ReleaseStringUTFChars(groundRecordingDirectory,str);
    return jptr(p);
}

JNI_METHOD(void, finalize)
(JNIEnv * env, jclass jclass1,jlong videoPlayerN) {
    VideoNative* p=native(videoPlayerN);
    delete (p);
}

JNI_METHOD(void, nativeAddConsumers)
(JNIEnv * env, jclass unused,jlong videoPlayerN,jobject surface) {
    native(videoPlayerN)->addConsumers(env,surface);
}

JNI_METHOD(void, nativeRemoveConsumers)
(JNIEnv *env,jclass jclass1, jlong videoPlayerN){
    native(videoPlayerN)->removeConsumers();
}

//This function is only called when the data for the video is coming from a file.
//In this case, the receiving is done via JAVA, and the received bytes are transfered from JAVA to NDK
JNI_METHOD(void, nativePassNALUData)
(JNIEnv *env,jclass jclass1,jlong videoPlayerN,jbyteArray b,jint offset,jint length){
    jbyte *arrayP=env->GetByteArrayElements(b, nullptr);
    auto * p=(uint8_t*)arrayP;
    native(videoPlayerN)->onNewVideoData(&p[(int) offset], length,false,true);
    env->ReleaseByteArrayElements(b,arrayP,0);
}

JNI_METHOD(void, nativeStartReceiver)
(JNIEnv * env, jclass jclass1,jlong videoPlayerN,jobject assetManager){
    AAssetManager* mgr=AAssetManager_fromJava(env,assetManager);
    native(videoPlayerN)->startReceiver(env, mgr);
}

JNI_METHOD(void, nativeStopReceiver)
(JNIEnv * env,jclass jclass1,jlong videoPlayerN){
    native(videoPlayerN)->stopReceiver();
}

JNI_METHOD(jstring , getVideoInfoString)
(JNIEnv *env,jclass jclass1,jlong testReceiverN) {
    VideoNative* p=native(testReceiverN);
    jstring ret = env->NewStringUTF(p->getInfoString().c_str());
    return ret;
}

JNI_METHOD(jboolean , anyVideoDataReceived)
(JNIEnv *env,jclass jclass1,jlong testReceiverN) {
    VideoNative* p=native(testReceiverN);
    if(p->mVideoReceiver){
        return (jboolean) false;
    }
    bool ret = (p->mVideoReceiver->getNReceivedBytes()  > 0);
    return (jboolean) ret;
}

JNI_METHOD(jboolean , receivingVideoButCannotParse)
(JNIEnv *env,jclass jclass1,jlong testReceiverN) {
    VideoNative* p=native(testReceiverN);
    if(p->mVideoReceiver){
        return (jboolean) (p->mVideoReceiver->getNReceivedBytes() > 1024 * 1024 && p->mParser.nParsedNALUs == 0);
    }
    return (jboolean) false;
}

JNI_METHOD(jboolean , anyVideoBytesParsedSinceLastCall)
(JNIEnv *env,jclass jclass1,jlong testReceiverN) {
    VideoNative* p=native(testReceiverN);
    long nalusSinceLast = p->mParser.nParsedNALUs - p->nNALUsAtLastCall;
    p->nNALUsAtLastCall += nalusSinceLast;
    return (jboolean) (nalusSinceLast > 0);
}

JNI_METHOD(void,nativeCallBack)
(JNIEnv *env,jclass jclass1,jobject videoParamsChangedI,jlong testReceiverN){
    VideoNative* p=native(testReceiverN);
    //Update all java stuff
    if(p->latestDecodingInfoChanged || p->latestVideoRatioChanged){
        jclass jClassExtendsIVideoParamsChanged= env->GetObjectClass(videoParamsChangedI);
        if(p->latestVideoRatioChanged){
            jmethodID onVideoRatioChangedJAVA = env->GetMethodID(jClassExtendsIVideoParamsChanged, "onVideoRatioChanged", "(II)V");
            env->CallVoidMethod(videoParamsChangedI,onVideoRatioChangedJAVA,(jint)p->latestVideoRatio.width,(jint)p->latestVideoRatio.height);
            p->latestVideoRatioChanged=false;
        }
        if(p->latestDecodingInfoChanged){
            jmethodID onDecodingInfoChangedJAVA = env->GetMethodID(jClassExtendsIVideoParamsChanged, "onDecodingInfoChanged", "(FFFFFII)V");
            const auto info=p->latestDecodingInfo;
            env->CallVoidMethod(videoParamsChangedI,onDecodingInfoChangedJAVA,(jfloat)info.currentFPS,(jfloat)info.currentKiloBitsPerSecond,
                                (jfloat)info.avgParsingTime_ms,(jfloat)info.avgWaitForInputBTime_ms,(jfloat)info.avgDecodingTime_ms,(jint)info.nNALU,(jint)info.nNALUSFeeded);
            p->latestDecodingInfoChanged=false;
        }
    }
}

}

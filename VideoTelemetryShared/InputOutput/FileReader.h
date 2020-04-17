//
// Created by Constantin on 8/8/2018.
//

#ifndef FPV_VR_FILERECEIVER_H
#define FPV_VR_FILERECEIVER_H

#include <android/asset_manager.h>
#include <vector>
#include <chrono>
#include <string>
#include <media/NdkMediaExtractor.h>
#include <thread>
#include <android/log.h>
#include <sstream>
#include <fstream>
#include "GroundRecorderFPV.hpp"

//Creates a new thread that 'receives' data from File and forwards data
//Via the RAW_DATA_CALLBACK. It does not specify the type of the forwarded data -
//If it is video data, the following holds true:
//RAW H264 NALUs,not specified how and when SPS/PPS come

class FileReader{
public:
    typedef std::function<void(const uint8_t[],std::size_t,GroundRecorderFPV::PACKET_TYPE)> RAW_DATA_CALLBACK;
private:
    // This one is to also forward data to telemetry receiver when using .fpv file
    std::vector<const RAW_DATA_CALLBACK>  onDataReceivedCallbacks;
    const std::size_t CHUNK_SIZE;
    //if assetManager!=nullptr the filename is relative to the assets directory,else normal filesystem
    std::string FILEPATH;
    //Cannot make const since the functions it is called with are not marked const
    AAssetManager* assetManager;
    std::unique_ptr<std::thread> mThread;
    std::atomic<bool> receiving;
    int nReceivedB=0;
    void receiveLoop();
public:
    /**
     * Does nothing until startReading is called
     * @param onDataReceivedCallback callback that is called with the loaded data from the receiving thread
     * @param chunkSize Determines how big the data chunks are that are fed to the parser. The smaller the chunk size
     * The faster does the onDataReceivedCallback() return
     * Therefore allowing the file receiver to stop and exit quicker
     */
    FileReader(std::size_t chunkSize=1024):
            CHUNK_SIZE(chunkSize){
    }
    /**
     * Create and start the receiving thread, which will run until stopReading() is called.
     * @param assetManager use nullptr for 'normal' files, else a valid android asset manager
     * @param FILEPATH Path to file,depends on assetManager if relative to file or asset
     */
    void startReading(AAssetManager* assetManager,const std::string FILEPATH){
        this->assetManager=assetManager;
        this->FILEPATH=FILEPATH;
        receiving=true;
        mThread=std::make_unique<std::thread>([this]{this->receiveLoop();} );
    }
    void addCallBack(const RAW_DATA_CALLBACK cb){
        onDataReceivedCallbacks.push_back(cb);
    }
    /**
     * After this call returns it is guaranteed that no more data will be fed trough the callback
     */
    void stopReadingIfStarted(){
        if(receiving== false){
            return;
        }
        receiving=false;
        if(mThread->joinable()){
            mThread->join();
        }
        mThread.reset();
    }
    int getNReceivedBytes(){
        return nReceivedB;
    }
private:
    /**
     * Pass all data divided in parts of data of size==CHUNK_SIZE
     * Returns when all data has been passed or stopReceiving is called
     */
    void passDataInChunks(const uint8_t data[],const size_t size,GroundRecorderFPV::PACKET_TYPE packetType);
};

#endif //FPV_VR_FILERECEIVER_H

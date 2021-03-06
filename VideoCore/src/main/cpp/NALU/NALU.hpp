//
// Created by Constantin on 2/6/2019.
//

#ifndef LIVE_VIDEO_10MS_ANDROID_NALU_H
#define LIVE_VIDEO_10MS_ANDROID_NALU_H

//https://github.com/Dash-Industry-Forum/Conformance-and-reference-source/blob/master/conformance/TSValidator/h264bitstream/h264_stream.h


#include <string>
#include <chrono>
#include <sstream>
#include <array>
#include <vector>
#include <h264_stream.h>
#include <android/log.h>
#include <AndroidLogger.hpp>
#include <variant>
#include <optional>

#include "H26X.hpp"


/**
 * A NALU either contains H264 data (default) or H265 data
 * NOTE: Only when copy constructing a NALU it owns the data, else it only holds a data pointer (that might get overwritten by the parser if you hold onto a NALU)
 * Also, H264 and H265 is slightly different
 */
class NALU{
private:
    static uint8_t* makeOwnedCopy(const uint8_t* data,size_t data_len){
        auto ret=new uint8_t[data_len];
        memcpy(ret,data,data_len);
        return ret;
    }
public:
    // test video white iceland: Max 1024*117. Video might not be decodable if its NALU buffers size exceed the limit
    // But a buffer size of 1MB accounts for 60fps video of up to 60MB/s or 480 Mbit/s. That should be plenty !
    static constexpr const auto NALU_MAXLEN=1024*1024;
    // Application should re-use NALU_BUFFER to avoid memory allocations
    using NALU_BUFFER=std::array<uint8_t,NALU_MAXLEN>;
    // Copy constructor allocates new buffer for data (heavy)
    NALU(const NALU& nalu):
    ownedData(std::vector<uint8_t>(nalu.getData(),nalu.getData()+nalu.getSize())),
    data(ownedData->data()),data_len(nalu.getSize()),creationTime(nalu.creationTime),IS_H265_PACKET(nalu.IS_H265_PACKET){
        //MLOGD<<"NALU copy constructor";
    }
    // Default constructor does not allocate a new buffer,only stores some pointer (light)
    NALU(const NALU_BUFFER& data1,const size_t data_length,const bool IS_H265_PACKET1=false,const std::chrono::steady_clock::time_point creationTime=std::chrono::steady_clock::now()):
            data(data1.data()),data_len(data_length),creationTime{creationTime},IS_H265_PACKET(IS_H265_PACKET1){
    };
    ~NALU()= default;
private:
    // With the default constructor a NALU does not own its memory. This saves us one memcpy. However, storing a NALU after the lifetime of the
    // Non-owned memory expired is also needed in some places, so the copy-constructor creates a copy of the non-owned data and stores it in a optional buffer
    // WARNING: Order is important here (Initializer list). Declare before data pointer
    const std::optional<std::vector<uint8_t>> ownedData={};
    //const NALU_BUFFER& data;
    const uint8_t* data;
    const size_t data_len;
public:
    const bool IS_H265_PACKET;
    const std::chrono::steady_clock::time_point creationTime;
public:
    // pointer to the NALU data with 0001 prefix
    const uint8_t* getData()const{
        return data;
    }
    // size of the NALU data with 0001 prefix
    const size_t getSize()const{
        return data_len;
    }
    //pointer to the NALU data without 0001 prefix
    const uint8_t* getDataWithoutPrefix()const{
        return &getData()[4];
    }
    //size of the NALU data without 0001 prefix
    const ssize_t getDataSizeWithoutPrefix()const{
        if(getSize()<=4)return 0;
        return getSize()-4;
    }
    bool isSPS()const{
        if(IS_H265_PACKET){
            return get_nal_unit_type()==H265::NAL_UNIT_SPS;
        }
        return (get_nal_unit_type() == NAL_UNIT_TYPE_SPS);
    }
    bool isPPS()const{
        if(IS_H265_PACKET){
            return get_nal_unit_type()==H265::NAL_UNIT_PPS;
        }
        return (get_nal_unit_type() == NAL_UNIT_TYPE_PPS);
    }
    bool isVPS()const{
        assert(IS_H265_PACKET);
        return get_nal_unit_type()==H265::NAL_UNIT_VPS;
    }
    int get_nal_unit_type()const{
        if(getSize()<5)return -1;
        if(IS_H265_PACKET){
            return (getData()[4] & 0x7E)>>1;
        }
        return getData()[4]&0x1f;
    }
    std::string get_nal_name()const{
        if(IS_H265_PACKET){
            return H265::get_nal_name(get_nal_unit_type());
        }
        return H264::get_nal_name(get_nal_unit_type());
    }

    //returns true if starts with 0001, false otherwise
    bool hasValidPrefix()const{
        return data[0]==0 && data[1]==0 &&data[2]==0 &&data[3]==1;
    }

    std::string dataAsString()const{
        std::stringstream ss;
        for(int i=0;i<getSize();i++){
            ss<<(int)data[i]<<",";
        }
        return ss.str();
    }

    //Returns video width and height if the NALU is an SPS
    std::array<int,2> getVideoWidthHeightSPS()const{
        assert(isSPS());
        //if(!isSPS()){
        //    return {-1,-1};
        //}
        if(IS_H265_PACKET){
            return {640,480};
        }else{
            h264_stream_t* h = h264_new();
            read_nal_unit(h,getDataWithoutPrefix(),(int)getDataSizeWithoutPrefix());
            sps_t* sps=h->sps;
            int Width = ((sps->pic_width_in_mbs_minus1 +1)*16) -sps->frame_crop_right_offset *2 -sps->frame_crop_left_offset *2;
            int Height = ((2 -sps->frame_mbs_only_flag)* (sps->pic_height_in_map_units_minus1 +1) * 16) - (sps->frame_crop_bottom_offset* 2) - (sps->frame_crop_top_offset* 2);
            h264_free(h);
            return {Width,Height};
        }
    }

    //Don't forget to free the h264 stream
    h264_stream_t* toH264Stream()const{
        h264_stream_t* h = h264_new();
        read_nal_unit(h,getDataWithoutPrefix(),(int)getDataSizeWithoutPrefix());
        return h;
    }

    void debugX()const{
        h264_stream_t* h = h264_new();
        read_debug_nal_unit(h,getDataWithoutPrefix(),(int)getDataSizeWithoutPrefix());
        h264_free(h);
    }

    //Create a NALU from h264stream object
    //Only tested on PSP/PPS !!!!!!!!!!
    //After copying data into the new NALU the h264_stream object is deleted
    //If the oldNALU!=nullptr the function checks if the new created nalu has the exact same length and also uses its creation timestamp
    //Example modifying sps:
    //if(nalu.isSPS()){
    //    h264_stream_t* h=nalu.toH264Stream();
    //    //Do manipulations to h->sps...
    //    modNALU=NALU::fromH264StreamAndFree(h,&nalu);
    //}
    /*static NALU* fromH264StreamAndFree(h264_stream_t* h,const NALU* oldNALU= nullptr){
        //The write function seems to be a bit buggy, e.g. its input buffer size needs to be stupid big
        std::vector<uint8_t> tmp(1024);
        int writeRet=write_nal_unit(h,tmp.data(),1024);
        tmp.insert(tmp.begin(),0);
        tmp.insert(tmp.begin(),0);
        tmp.insert(tmp.begin(),0);
        tmp.at(3)=1;
        writeRet+=3;
        //allocate memory for the new NALU
        uint8_t* newNaluData=new uint8_t[writeRet];
        memcpy(newNaluData,tmp.data(),(size_t)writeRet);
        if(oldNALU!= nullptr){
            if(oldNALU->data.size()!=writeRet){
                __android_log_print(ANDROID_LOG_ERROR,"NALU","Error h264bitstream %d %d",(int)oldNALU->data.size(),writeRet);
            }
            return new NALU(newNaluData,(size_t)writeRet,oldNALU->creationTime);
        }
        return new NALU(newNaluData,(size_t)writeRet);
    }*/
};

typedef std::function<void(const NALU& nalu)> NALU_DATA_CALLBACK;


#endif //LIVE_VIDEO_10MS_ANDROID_NALU_H

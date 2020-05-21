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

//A NALU consists of
//a) DATA buffer
//b) buffer length
//c) creation time
//Does NOT own the data

class NALU{
public:
    // test video white iceland: Max 1024*117. Video might not be decodable if its NALU buffers size exceed the limit
    static constexpr const auto NALU_MAXLEN=1024*1024;
    // Application should re-use NALU_BUFFER to avoid memory allocations
    using NALU_BUFFER=std::array<uint8_t,NALU_MAXLEN>;
    //Copy constructor
    //NALU(const NALU& nalu):data(nalu.data.begin(),nalu.data.end()),creationTime(std::chrono::steady_clock::now()){
    //    MLOGD<<"Copy is called";
    //}
    NALU(const NALU& nalu)= default;
    NALU(const uint8_t* data1,const size_t data_length,const std::chrono::steady_clock::time_point creationTime=std::chrono::steady_clock::now()):
    data(data1,data1+data_length),
        creationTime{creationTime}{
        //check();
    };
    NALU(const std::vector<uint8_t> data1,const std::chrono::steady_clock::time_point creationTime=std::chrono::steady_clock::now()):
            data(data1),
            creationTime{creationTime}{
        //check();
        //const bool equal=memcmp(data.data(),data1.data(),data.size());
        //MLOGD<<"Size is"<<data.size()<<"Equal"<<equal;
    };
private:
    std::vector<uint8_t> data;
public:
    const std::chrono::steady_clock::time_point creationTime;
public:
    const size_t getSize()const{
        return data.size();
    }
    const uint8_t* getData()const{
        return data.data();
    }
    bool isSPS()const{
        return (get_nal_unit_type() == NAL_UNIT_TYPE_SPS);
    }
    bool isPPS()const{
        return (get_nal_unit_type() == NAL_UNIT_TYPE_PPS);
    }
    int get_nal_unit_type()const{
        if(data.size()<5)return -1;
        return data[4]&0x1f;
    }
    //not safe if data_length<=4;
    //returns pointer to data without the 0001 prefix
    const uint8_t* getDataWithoutPrefix()const{
        return &data[4];
    }
    const ssize_t getDataSizeWithoutPrefix()const{
        if(data.size()<=4)return 0;
        return data.size()-4;
    }
    static std::string get_nal_name(int nal_unit_type){
       std::string nal_unit_type_name;
       switch (nal_unit_type)
       {
           case  NAL_UNIT_TYPE_UNSPECIFIED :                   nal_unit_type_name = "Unspecified"; break;
           case  NAL_UNIT_TYPE_CODED_SLICE_NON_IDR :           nal_unit_type_name = "Coded slice of a non-IDR picture"; break;
           case  NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A :  nal_unit_type_name = "Coded slice data partition A"; break;
           case  NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_B :  nal_unit_type_name = "Coded slice data partition B"; break;
           case  NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_C :  nal_unit_type_name = "Coded slice data partition C"; break;
           case  NAL_UNIT_TYPE_CODED_SLICE_IDR :               nal_unit_type_name = "Coded slice of an IDR picture"; break;
           case  NAL_UNIT_TYPE_SEI :                           nal_unit_type_name = "Supplemental enhancement information (SEI)"; break;
           case  NAL_UNIT_TYPE_SPS :                           nal_unit_type_name = "Sequence parameter set"; break;
           case  NAL_UNIT_TYPE_PPS :                           nal_unit_type_name = "Picture parameter set"; break;
           case  NAL_UNIT_TYPE_AUD :                           nal_unit_type_name = "Access unit delimiter"; break;
           case  NAL_UNIT_TYPE_END_OF_SEQUENCE :               nal_unit_type_name = "End of sequence"; break;
           case  NAL_UNIT_TYPE_END_OF_STREAM :                 nal_unit_type_name = "End of stream"; break;
           case  NAL_UNIT_TYPE_FILLER :                        nal_unit_type_name = "Filler data"; break;
           case  NAL_UNIT_TYPE_SPS_EXT :                       nal_unit_type_name = "Sequence parameter set extension"; break;
               // 14..18    // Reserved
           case  NAL_UNIT_TYPE_CODED_SLICE_AUX :               nal_unit_type_name = "Coded slice of an auxiliary coded picture without partitioning"; break;
               // 20..23    // Reserved
               // 24..31    // Unspecified
           default :                                           nal_unit_type_name = std::string("Unknown")+std::to_string(nal_unit_type); break;
       }
       // __android_log_print(ANDROID_LOG_DEBUG,"NALU","code %s len %d",nal_unit_type_name.c_str(),(int)data_length);

       return nal_unit_type_name;
   };

    std::string get_nal_name()const{
        return get_nal_name(get_nal_unit_type());
    }

    //returns true if starts with 0001, false otherwise
    bool hasValidPrefix()const{
        return data[0]==0 && data[1]==0 &&data[2]==0 &&data[3]==1;
    }

    std::string dataAsString()const{
        std::stringstream ss;
        for(int i=0;i<data.size();i++){
            ss<<(int)data[i]<<",";
        }
        return ss.str();
    }


    //Returns video width and height if the NALU is an SPS
    std::array<int,2> getVideoWidthHeightSPS()const{
        if(!isSPS()){
            return {-1,-1};
        }
        h264_stream_t* h = h264_new();
        read_nal_unit(h,getDataWithoutPrefix(),(int)getDataSizeWithoutPrefix());
        sps_t* sps=h->sps;
        int Width = ((sps->pic_width_in_mbs_minus1 +1)*16) -sps->frame_crop_right_offset *2 -sps->frame_crop_left_offset *2;
        int Height = ((2 -sps->frame_mbs_only_flag)* (sps->pic_height_in_map_units_minus1 +1) * 16) - (sps->frame_crop_bottom_offset* 2) - (sps->frame_crop_top_offset* 2);
        h264_free(h);
        return {Width,Height};
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

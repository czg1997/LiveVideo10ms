//
// Created by Constantin on 2/6/2019.
//

#ifndef LIVE_VIDEO_10MS_ANDROID_PARSERTP_H
#define LIVE_VIDEO_10MS_ANDROID_PARSERTP_H

#include <cstdio>
#include "../NALU/NALU.hpp"

/*********************************************
 ** Parses a stream of rtp h264 data into NALUs
**********************************************/
class RTPDecoder{
public:
    RTPDecoder(NALU_DATA_CALLBACK cb);
public:
    //Decoding
    void parseRTPtoNALU(const uint8_t* rtp_data, const size_t data_length);
    void reset();
    // Returns the sequence number of an RTP packet
    static int getSequenceNumber(const uint8_t* rtp_data,const size_t data_len);
private:
    const NALU_DATA_CALLBACK cb;
    std::array<uint8_t,NALU::NALU_MAXLEN> mNALU_DATA;
    size_t mNALU_DATA_LENGTH=0;
};

/*********************************************
 ** Parses a stream of h264 NALUs into RTP packets
**********************************************/
class RTPEncoder{
public:
    struct RTPPacket{
        const uint8_t* data;
        const size_t data_len;
    };
    typedef std::function<void(const RTPPacket& rtpPacket)> RTP_DATA_CALLBACK;
public:
    RTPEncoder(RTP_DATA_CALLBACK cb): mCB(cb){};
    int parseNALtoRTP(int framerate, const uint8_t *nalu_data,const size_t nalu_data_len);
private:
    RTP_DATA_CALLBACK mCB;
    void forwardRTPPacket(uint8_t *rtp_packet, size_t rtp_packet_len);
    //
    static constexpr std::size_t RTP_PAYLOAD_MAX_SIZE=1024;
    static constexpr std::size_t RTP_PACKET_MAX_SIZE=RTP_PAYLOAD_MAX_SIZE+1024;
    static constexpr std::size_t SEND_BUF_SIZE=RTP_PAYLOAD_MAX_SIZE+1024;
    uint8_t mRTP_BUFF_SEND[SEND_BUF_SIZE];
    uint16_t seq_num = 0;
    uint32_t ts_current = 0;
};

class TestEncodeDecodeRTP{
private:
    void onNALU(const NALU& nalu);
    void onRTP(const RTPEncoder::RTPPacket& packet);
    std::unique_ptr<RTPEncoder> encoder;
    std::unique_ptr<RTPDecoder> decoder;
    std::unique_ptr<NALU> lastNALU;
public:
    TestEncodeDecodeRTP();
    // This encodes the nalu to RTP then decodes it again
    // After that, check that their contents match
    void testEncodeDecodeRTP(const NALU& nalu);
};

#endif //LIVE_VIDEO_10MS_ANDROID_PARSERTP_H

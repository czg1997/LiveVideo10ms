//
// Created by Constantin on 2/6/2019.
//

#include "ParseRTP.h"
#include <AndroidLogger.hpp>
#include <arpa/inet.h>

//changed "unsigned char" to uint8_t
typedef struct rtp_header {
#if __BYTE_ORDER == __BIG_ENDIAN
    //For big endian
    uint8_t version:2;       // Version, currently 2
    uint8_t padding:1;       // Padding bit
    uint8_t extension:1;     // Extension bit
    uint8_t cc:4;            // CSRC count
    uint8_t marker:1;        // Marker bit
    uint8_t payload:7;       // Payload type
#else
    //For little endian
    uint8_t cc:4;            // CSRC count
    uint8_t extension:1;     // Extension bit
    uint8_t padding:1;       // Padding bit
    uint8_t version:2;       // Version, currently 2
    uint8_t payload:7;       // Payload type
    uint8_t marker:1;        // Marker bit
#endif
    uint16_t sequence;        // sequence number
    uint32_t timestamp;       //  timestamp
    uint32_t sources;      // contributing sources
} __attribute__ ((packed)) rtp_header_t; /* 12 bytes */
//NOTE: sequence,timestamp and sources has to be converted to the right endian using htonl/htons

//Taken from https://github.com/hmgle/h264_to_rtp/blob/master/h264tortp.h
typedef struct nalu_header {
    uint8_t type:   5;  /* bit: 0~4 */
    uint8_t nri:    2;  /* bit: 5~6 */
    uint8_t f:      1;  /* bit: 7 */
} __attribute__ ((packed)) nalu_header_t; /* 1 bytes */
typedef struct fu_header {
    uint8_t type:   5;
    uint8_t r:      1;
    uint8_t e:      1;
    uint8_t s:      1;
} __attribute__ ((packed)) fu_header_t; /* 1 bytes */

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
typedef struct fu_indicator {
    /* byte 0 */
    uint8_t type:   5;
    uint8_t nri:    2;
    uint8_t f:      1;
} __attribute__ ((packed)) fu_indicator_t; /* 1 bytes */
static constexpr auto RTP_PAYLOAD_TYPE_H264=96;
static constexpr auto MY_SSRC_NUM=10;

typedef struct nalu_header_h265{
    uint16_t payloadHdr;
    uint16_t donl;
}__attribute__ ((packed)) nalu_header_h265_t;

RTPDecoder::RTPDecoder(NALU_DATA_CALLBACK cb): cb(std::move(cb)){
}

void RTPDecoder::reset(){
    mNALU_DATA_LENGTH=0;
    //nalu_data.reserve(NALU::NALU_MAXLEN);
}

static void debugRtpHeader(const rtp_header_t* rtp_header){
    std::stringstream ss;
    ss<<"cc"<<(int)rtp_header->cc<<"\n";
    ss<<"extension"<<(int)rtp_header->extension<<"\n";
    ss<<"padding"<<(int)rtp_header->padding<<"\n";
    ss<<"version"<<(int)rtp_header->version<<"\n";
    ss<<"payload"<<(int)rtp_header->payload<<"\n";
    ss<<"marker"<<(int)rtp_header->marker<<"\n";
    ss<<"sequence"<<(int)htons(rtp_header->sequence)<<"\n";
    ss<<"timestamp"<<(int)rtp_header->timestamp<<"\n";
    ss<<"sources"<<(int)rtp_header->sources<<"\n";
    MLOGD<<"RTP Header: "<<ss.str();
}

void RTPDecoder::parseRTPtoNALU(const uint8_t* rtp_data, const size_t data_length){
    //12 rtp header bytes and 1 nalu_header_t type byte
    if(data_length <= sizeof(rtp_header_t)+sizeof(nalu_header_t)){
        MLOGD<<"Not enough rtp data";
        return;
    }
    MLOGD<<"Got rtp data";
    // Testing regarding sequence numbers.This stuff an be removed without issues
    const int seqNr=getSequenceNumber(rtp_data,data_length);
    if(seqNr==lastSequenceNumber){
        // duplicate. This should never happen for 'normal' rtp streams, but can be usefully when testing bitrates
        // (Since you can send the same packet multiple times to emulate a higher bitrate)
        MLOGD<<"Same seqNr";
        //return;
    }
    if(lastSequenceNumber==-1){
        // first packet in stream
        flagPacketHasGoneMissing=false;
    }else{
        // Don't forget that the sequence number loops every UINT16_MAX packets
        if(seqNr != ((lastSequenceNumber+1) % UINT16_MAX)){
            // We are missing a Packet !
            MLOGD<<"missing a packet. Last:"<<lastSequenceNumber<<" Curr:"<<seqNr<<" Diff:"<<(seqNr-(int)lastSequenceNumber);
            //flagPacketHasGoneMissing=true;
        }
    }
    lastSequenceNumber=seqNr;

    const auto* rtp_header=(rtp_header_t*)&rtp_data[0];
    debugRtpHeader(rtp_header);
    //  24576
    if(rtp_header->payload==96){
        MLOGD<<"Is h264";
    }else if(rtp_header->payload==97){
        MLOGD<<"Is h265";
    }
    const auto* nalu_header=(nalu_header_t *)&rtp_data[sizeof(rtp_header_t)];

    if (nalu_header->type == 28) { /* FU-A */
        MLOGD<<"Got partial NALU";
        const fu_header_t* fu_header = (fu_header_t*)&rtp_data[13];
        if (fu_header->e == 1) {
            /* end of fu-a */
            memcpy(&mNALU_DATA[mNALU_DATA_LENGTH], &rtp_data[14], (size_t)data_length - 14);
            mNALU_DATA_LENGTH+= data_length - 14;
            if(!flagPacketHasGoneMissing){
                // To better measure latency we can actually use the timestamp from when the first bytes for this packet were received
                forwardNALU(timePointStartOfReceivingNALU);
            }
            mNALU_DATA_LENGTH=0;
        } else if (fu_header->s == 1) {
            timePointStartOfReceivingNALU=std::chrono::steady_clock::now();
            // Beginning of new fu sequence - we can remove the 'drop packet' flag
            if(flagPacketHasGoneMissing){
                MLOGD<<"Got fu-a start - clearing missing packet flag";
                flagPacketHasGoneMissing=false;
            }
            /* start of fu-a */
            mNALU_DATA[0]=0;
            mNALU_DATA[1]=0;
            mNALU_DATA[2]=0;
            mNALU_DATA[3]=1;
            mNALU_DATA_LENGTH=4;
            const uint8_t h264_nal_header = (uint8_t)(fu_header->type & 0x1f)
                                            | (nalu_header->nri << 5)
                                            | (nalu_header->f << 7);
            mNALU_DATA[4]=h264_nal_header;
            mNALU_DATA_LENGTH++;
            memcpy(&mNALU_DATA[mNALU_DATA_LENGTH], &rtp_data[14], (size_t)data_length - 14);
            mNALU_DATA_LENGTH+= data_length - 14;
        } else {
            /* middle of fu-a */
            memcpy(&mNALU_DATA[mNALU_DATA_LENGTH], &rtp_data[14], (size_t)data_length - 14);
            mNALU_DATA_LENGTH+= data_length - 14;
        }
        //LOGV("partially nalu");
    } else if(nalu_header->type>0 && nalu_header->type<24){
        MLOGD<<"Got full nalu";
        timePointStartOfReceivingNALU=std::chrono::steady_clock::now();
        // Full NALU - we can remove the 'drop packet' flag
        if(flagPacketHasGoneMissing){
            MLOGD<<"Got full NALU - clearing missing packet flag";
            flagPacketHasGoneMissing= false;
        }
        /* full nalu */
        mNALU_DATA[0]=0;
        mNALU_DATA[1]=0;
        mNALU_DATA[2]=0;
        mNALU_DATA[3]=1;
        mNALU_DATA_LENGTH=4;
        const uint8_t h264_nal_header = (uint8_t )(nalu_header->type & 0x1f)
                                        | (nalu_header->nri << 5)
                                        | (nalu_header->f << 7);
        mNALU_DATA[4]=h264_nal_header;
        mNALU_DATA_LENGTH++;
        memcpy(&mNALU_DATA[mNALU_DATA_LENGTH], &rtp_data[13], (size_t)data_length - 13);
        mNALU_DATA_LENGTH+= data_length - 13;
        forwardNALU(timePointStartOfReceivingNALU);
        mNALU_DATA_LENGTH=0;
    }else{
        //MLOGD<<"header:"<<nalu_header->type;
    }
}

void RTPDecoder::parseRTPH265toNALU(const uint8_t* rtp_data, const size_t data_length){
    //12 rtp header bytes and 1 nalu_header_t type byte
    if(data_length <= sizeof(rtp_header_t)+sizeof(nalu_header_t)){
        MLOGD<<"Not enough rtp data";
        return;
    }
    MLOGD<<"Got rtp data";
    // Testing regarding sequence numbers.This stuff an be removed without issues
    const int seqNr=getSequenceNumber(rtp_data,data_length);
    if(seqNr==lastSequenceNumber){
        // duplicate. This should never happen for 'normal' rtp streams, but can be usefully when testing bitrates
        // (Since you can send the same packet multiple times to emulate a higher bitrate)
        MLOGD<<"Same seqNr";
        //return;
    }
    if(lastSequenceNumber==-1){
        // first packet in stream
        flagPacketHasGoneMissing=false;
    }else{
        // Don't forget that the sequence number loops every UINT16_MAX packets
        if(seqNr != ((lastSequenceNumber+1) % UINT16_MAX)){
            // We are missing a Packet !
            MLOGD<<"missing a packet. Last:"<<lastSequenceNumber<<" Curr:"<<seqNr<<" Diff:"<<(seqNr-(int)lastSequenceNumber);
            //flagPacketHasGoneMissing=true;
        }
    }
    lastSequenceNumber=seqNr;

    const auto* rtp_header=(rtp_header_t*)&rtp_data[0];
    debugRtpHeader(rtp_header);
    //  24576
    if(rtp_header->payload==96){
        MLOGD<<"Is payload type 96";
    }
    const auto* nalu_header=(nalu_header_h265_t*)&rtp_data[sizeof(rtp_header_t)];
    const auto* nalUnitPayloadData=&rtp_data[sizeof(rtp_header_t) + sizeof(nalu_header_h265_t)];
    const size_t nalUnitPayloadDataSize= data_length - sizeof(rtp_header_t) + sizeof(nalu_header_h265_t);

    MLOGD<<"H265 NALU hdr: "<<((int)nalu_header->payloadHdr)<<" "<<((int)nalu_header->donl);
    mNALU_DATA[0]=0;
    mNALU_DATA[1]=0;
    mNALU_DATA[2]=0;
    mNALU_DATA[3]=1;
    mNALU_DATA_LENGTH=4;
    memcpy(&mNALU_DATA[mNALU_DATA_LENGTH], nalUnitPayloadData, nalUnitPayloadDataSize);
    mNALU_DATA_LENGTH+=nalUnitPayloadDataSize;
    forwardNALU(std::chrono::steady_clock::now(),true);
}

void RTPDecoder::forwardNALU(const std::chrono::steady_clock::time_point creationTime,const bool isH265) {
    if(cb!= nullptr){
        NALU nalu(mNALU_DATA, mNALU_DATA_LENGTH,isH265,creationTime);
        //nalu_data.resize(nalu_data_length);
        //NALU nalu(nalu_data);
        cb(nalu);
    }
    mNALU_DATA_LENGTH=0;
}

int RTPDecoder::getSequenceNumber(const uint8_t* rtp_data,const size_t data_len) {
    if(data_len<sizeof(rtp_header_t)){
        return -1;
    }
    const rtp_header_t* rtp_header=(rtp_header_t*)rtp_data;
    const auto seqNr=rtp_header->sequence;
    return htons(seqNr);
    //return seqNr;
}


// xxxxxxxxxxxxxxxxxxxxxxxxxxx RTPEncoder part xxxxxxxxxxxxxxxxxxxxxxxxxxx

int RTPEncoder::parseNALtoRTP(int framerate, const uint8_t *nalu_data, const size_t nalu_data_len) {
    // Watch out for not enough data (else algorithm might crash)
    if(nalu_data_len <= 5){
        return -1;
    }
    // Prefix is the 0,0,0,1. RTP does not use it
    const uint8_t *nalu_buf_without_prefix = &nalu_data[4];
    const size_t nalu_len_without_prefix= nalu_data_len - 4;

    ts_current += (90000 / framerate);  /* 90000 / 25 = 3600 */

    if (nalu_len_without_prefix <= RTP_PAYLOAD_MAX_SIZE) {
        /*
         * single nal unit
         */
        memset(mRTP_BUFF_SEND, 0, sizeof(rtp_header_t) + sizeof(nalu_header_t));
        /**
         * Set pointer for headers
         */
        auto* rtp_hdr= (rtp_header_t *)mRTP_BUFF_SEND;
        auto* nalu_hdr = (nalu_header_t *)&mRTP_BUFF_SEND[sizeof(rtp_header_t)];
        /**
         * Write rtp header
         */
        rtp_hdr->cc = 0;
        rtp_hdr->extension = 0;
        rtp_hdr->padding = 0;
        rtp_hdr->version = 2;
        rtp_hdr->payload = RTP_PAYLOAD_TYPE_H264;
        // rtp_hdr->marker = (pstStream->u32PackCount - 1 == i) ? 1 : 0;   /* If the packet is the end of a frame, set it to 1, otherwise it is 0. rfc 1889 does not specify the purpose of this bit*/
        rtp_hdr->marker=0;
        rtp_hdr->sequence = htons(++seq_num % UINT16_MAX);
        rtp_hdr->timestamp = htonl(ts_current);
        //rtp_hdr->timestamp=0;
        rtp_hdr->sources = htonl(MY_SSRC_NUM);
        /*
         * Set rtp load single nal unit header
         */
        nalu_hdr->f = (nalu_buf_without_prefix[0] & 0x80) >> 7;        /* bit0 */
        nalu_hdr->nri = (nalu_buf_without_prefix[0] & 0x60) >> 5;      /* bit1~2 */
        nalu_hdr->type = (nalu_buf_without_prefix[0] & 0x1f);
        //MLOGD<<"ENC NALU hdr type"<<((int)nalu_hdr->type);
        /*
         * 3.Fill nal content
         */
        memcpy(mRTP_BUFF_SEND + 13, nalu_buf_without_prefix + 1, nalu_len_without_prefix - 1);    /* 不拷贝nalu头 */
        /*
         * 4. Forward the RTP packet
         */
        const size_t len_sendbuf = 12 + nalu_len_without_prefix;
        forwardRTPPacket(mRTP_BUFF_SEND, len_sendbuf);
        //
        //MLOGD<<"NALU <RTP_PAYLOAD_MAX_SIZE";
    } else {    /* nalu_len > RTP_PAYLOAD_MAX_SIZE */
        //MLOGD<<"NALU >RTP_PAYLOAD_MAX_SIZE";
        //assert(false);
        /*
         * FU-A segmentation
         */
        /*
         * 1. Count the number of divisions
         *
         * Except for the last shard，
         * Consumption per shard RTP_PAYLOAD_MAX_SIZE BYTE
         */
        /* The number of splits when nalu needs to be split to send */
        const int fu_pack_num = nalu_len_without_prefix % RTP_PAYLOAD_MAX_SIZE ? (nalu_len_without_prefix / RTP_PAYLOAD_MAX_SIZE + 1) : nalu_len_without_prefix / RTP_PAYLOAD_MAX_SIZE;
        /* The size of the last shard */
        const int last_fu_pack_size = nalu_len_without_prefix % RTP_PAYLOAD_MAX_SIZE ? nalu_len_without_prefix % RTP_PAYLOAD_MAX_SIZE : RTP_PAYLOAD_MAX_SIZE;
        /* fu-A Serial number */
        for (int fu_seq = 0; fu_seq < fu_pack_num; fu_seq++) {
            memset(mRTP_BUFF_SEND, 0, sizeof(rtp_header_t) + sizeof(fu_indicator_t) + sizeof(fu_header_t));
            //
            auto* rtp_hdr = (rtp_header_t *)mRTP_BUFF_SEND;
            auto* fu_ind = (fu_indicator_t *)&mRTP_BUFF_SEND[sizeof(rtp_header_t)];
            auto* fu_hdr = (fu_header_t *)&mRTP_BUFF_SEND[sizeof(rtp_header_t) + sizeof(fu_indicator_t)];
            /*
             * 根据FU-A的类型设置不同的rtp头和rtp荷载头
             */
            if (fu_seq == 0) {  /* 第一个FU-A */
                /*
                 * 1. 设置 rtp 头
                 */
                rtp_hdr->cc = 0;
                rtp_hdr->extension = 0;
                rtp_hdr->padding = 0;
                rtp_hdr->version = 2;
                rtp_hdr->payload = RTP_PAYLOAD_TYPE_H264;
                rtp_hdr->marker = 0;    /* If the packet is the end of a frame, set it to 1, otherwise it is 0. rfc 1889 does not specify the purpose of this bit*/
                rtp_hdr->sequence = htons(++seq_num % UINT16_MAX);
                rtp_hdr->timestamp = htonl(ts_current);
                rtp_hdr->sources = htonl(MY_SSRC_NUM);
                /*
                 * 2. 设置 rtp 荷载头部
                 */
                fu_ind->f = (nalu_buf_without_prefix[0] & 0x80) >> 7;
                fu_ind->nri = (nalu_buf_without_prefix[0] & 0x60) >> 5;
                fu_ind->type = 28;
                //
                fu_hdr->s = 1;
                fu_hdr->e = 0;
                fu_hdr->r = 0;
                fu_hdr->type = nalu_buf_without_prefix[0] & 0x1f;
                /*
                 * 3. 填充nalu内容
                 */
                memcpy(mRTP_BUFF_SEND + 14, nalu_buf_without_prefix + 1, RTP_PAYLOAD_MAX_SIZE - 1);    /* 不拷贝nalu头 */
                /*
                 * 4. 发送打包好的rtp包到客户端
                 */
                const size_t len_sendbuf = 12 + 2 + (RTP_PAYLOAD_MAX_SIZE - 1);  /* rtp头 + nalu头 + nalu内容 */
                forwardRTPPacket(mRTP_BUFF_SEND, len_sendbuf);

            } else if (fu_seq < fu_pack_num - 1) { /* 中间的FU-A */
                /*
                 * 1. 设置 rtp 头
                 */
                rtp_hdr->cc = 0;
                rtp_hdr->extension = 0;
                rtp_hdr->padding = 0;
                rtp_hdr->version = 2;
                rtp_hdr->payload = RTP_PAYLOAD_TYPE_H264;
                rtp_hdr->marker = 0;    /* 该包为一帧的结尾则置为1, 否则为0. rfc 1889 没有规定该位的用途 */
                rtp_hdr->sequence = htons(++seq_num % UINT16_MAX);
                rtp_hdr->timestamp = htonl(ts_current);
                rtp_hdr->sources = htonl(MY_SSRC_NUM);
                /*
                 * 2. 设置 rtp 荷载头部
                 */
                fu_ind->f = (nalu_buf_without_prefix[0] & 0x80) >> 7;
                fu_ind->nri = (nalu_buf_without_prefix[0] & 0x60) >> 5;
                fu_ind->type = 28;
                //
                fu_hdr->s = 0;
                fu_hdr->e = 0;
                fu_hdr->r = 0;
                fu_hdr->type = nalu_buf_without_prefix[0] & 0x1f;
                /*
                 * 3. 填充nalu内容
                 */
                memcpy(mRTP_BUFF_SEND + 14, nalu_buf_without_prefix + RTP_PAYLOAD_MAX_SIZE * fu_seq, RTP_PAYLOAD_MAX_SIZE);    /* 不拷贝nalu头 */
                /*
                 * 4. 发送打包好的rtp包到客户端
                 */
                const size_t len_sendbuf = 12 + 2 + RTP_PAYLOAD_MAX_SIZE;
                forwardRTPPacket(mRTP_BUFF_SEND, len_sendbuf);
            } else { /* 最后一个FU-A */
                /*
                 * 1. 设置 rtp 头
                 */
                rtp_hdr->cc = 0;
                rtp_hdr->extension = 0;
                rtp_hdr->padding = 0;
                rtp_hdr->version = 2;
                rtp_hdr->payload = RTP_PAYLOAD_TYPE_H264;
                rtp_hdr->marker = 1;    /* 该包为一帧的结尾则置为1, 否则为0. rfc 1889 没有规定该位的用途 */
                rtp_hdr->sequence = htons(++seq_num % UINT16_MAX);
                rtp_hdr->timestamp = htonl(ts_current);
                rtp_hdr->sources = htonl(MY_SSRC_NUM);
                /*
                 * 2. 设置 rtp 荷载头部
                 */
                fu_ind->f = (nalu_buf_without_prefix[0] & 0x80) >> 7;
                fu_ind->nri = (nalu_buf_without_prefix[0] & 0x60) >> 5;
                fu_ind->type = 28;
                //
                fu_hdr->s = 0;
                fu_hdr->e = 1;
                fu_hdr->r = 0;
                fu_hdr->type = nalu_buf_without_prefix[0] & 0x1f;
                /*
                 * 3. 填充rtp荷载
                 */
                memcpy(mRTP_BUFF_SEND + 14, nalu_buf_without_prefix + RTP_PAYLOAD_MAX_SIZE * fu_seq, last_fu_pack_size);    /* 不拷贝nalu头 */
                /*
                 * 4. 发送打包好的rtp包到客户端
                 */
                const size_t len_sendbuf = 12 + 2 + last_fu_pack_size;
                forwardRTPPacket(mRTP_BUFF_SEND, len_sendbuf);

            } /* else-if (fu_seq == 0) */
        } /* end of for (fu_seq = 0; fu_seq < fu_pack_num; fu_seq++) */

    } /* end of else-if (nalu_len <= RTP_PAYLOAD_MAX_SIZE) */
    return 0;
}


void RTPEncoder::forwardRTPPacket(uint8_t *rtp_packet, size_t rtp_packet_len) {
    assert(rtp_packet_len!=0);
    assert(rtp_packet_len<=RTP_PACKET_MAX_SIZE);
    //MLOGD<<"forwardRTPPacket of size "<<rtp_packet_len;
    if(mCB!= nullptr){
        mCB({rtp_packet,rtp_packet_len});
    }else{
        MLOGE<<"No RTP Encoder callback set";
    }
}

TestEncodeDecodeRTP::TestEncodeDecodeRTP() {
    decoder=std::make_unique<RTPDecoder>(std::bind(&TestEncodeDecodeRTP::onNALU, this, std::placeholders::_1));
    encoder=std::make_unique<RTPEncoder>(std::bind(&TestEncodeDecodeRTP::onRTP, this, std::placeholders::_1));
}

void TestEncodeDecodeRTP::testEncodeDecodeRTP(const NALU& nalu) {
    if(nalu.getSize()<=6)return;
    encoder->parseNALtoRTP(30,nalu.getData(),nalu.getSize());
    //
    assert(lastNALU!=nullptr);
    assert(lastNALU->getSize()==nalu.getSize());
    const bool contentEquals=memcmp(nalu.getData(),lastNALU->getData(),nalu.getSize())==0;
    assert(contentEquals==true);
    lastNALU.reset();
}

void TestEncodeDecodeRTP::onRTP(const RTPEncoder::RTPPacket &packet) {
    decoder->parseRTPtoNALU(packet.data,packet.data_len);
}

void TestEncodeDecodeRTP::onNALU(const NALU &nalu) {
    // If we feed one NALU we should only get one NALU out
    assert(lastNALU==nullptr);
    lastNALU=std::make_unique<NALU>(nalu);
}

// PHZ
// 2018-5-16

#ifndef XOP_MEDIA_H
#define XOP_MEDIA_H

#include <cstdint>
#include <vector>

namespace xop
{

/* RTSP supported media types */
enum MediaType
{
	PCMU = 0,
	PCMA = 8,
	H264 = 96,
	AAC  = 37,
	AV1  = 98,
	H265 = 265,
	NONE
};	

enum FrameType
{
	VIDEO_FRAME_I = 0x01,	  
	VIDEO_FRAME_P = 0x02,
	VIDEO_FRAME_B = 0x03,    
	AUDIO_FRAME   = 0x11,   
};

struct AVFrame
{
	AVFrame() : type(0), timestamp(0), last(1) {}
	AVFrame(const uint8_t *data, std::size_t size) : AVFrame()
	{
		buffer.reserve(size);
		buffer.assign(data, data + size);
	}

	std::vector<uint8_t> buffer;     /* Frame data */
	uint8_t  type;				     /* Frame type */
	int64_t timestamp;		  	     /* Timestamp */
	uint8_t  last;                   /* RTP marker bit (1=last packet of frame) */
};

static const int MAX_MEDIA_CHANNEL = 2;
static const int MAX_CLIENTS = 3;  /* Embedded systems: max 3 concurrent clients */
/* RTP packet pool capacity (x 1600 B = 600 KB, allocated once). Must exceed
   one client's write queue (256 packets, TcpConnection::kMaxWriteQueue) plus
   an IDR frame (~70-140 packets at 1080p), since packets are shared. When it
   runs dry, frames are dropped rather than taken from the heap. */
static const int RTP_PACKET_POOL_SIZE = 384;

enum MediaChannelId
{
	channel_0,
	channel_1
};

typedef uint32_t MediaSessionId;

}

#endif


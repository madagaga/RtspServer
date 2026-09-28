// PHZ
// 2018-5-15

#ifndef XOP_BUFFER_WRITER_H
#define XOP_BUFFER_WRITER_H

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include "Socket.h"

namespace xop
{

void WriteUint32BE(char* p, uint32_t value);
void WriteUint32LE(char* p, uint32_t value);
void WriteUint24BE(char* p, uint32_t value);
void WriteUint24LE(char* p, uint32_t value);
void WriteUint16BE(char* p, uint16_t value);
void WriteUint16LE(char* p, uint16_t value);
	
class BufferWriter
{
public:
	BufferWriter(int capacity = kMaxQueueLength);
	~BufferWriter() {}

	bool Append(std::shared_ptr<char> data, uint32_t size, uint32_t index=0);
	bool Append(const char* data, uint32_t size, uint32_t index=0);
	/* Queue a private header (copied, <= kMaxHeaderSize) followed by a shared
	   payload (referenced, never copied nor written). */
	bool Append(const uint8_t* header, uint32_t header_size,
	            std::shared_ptr<uint8_t> payload, uint32_t payload_offset, uint32_t payload_size);
	int Send(SOCKET sockfd, int timeout=0);

	bool IsEmpty() const 
	{ return buffer_.empty(); }

	bool IsFull() const 
	{ return ((int)buffer_.size() >= max_queue_length_ ? true : false); }

	uint32_t Size() const
	{ return (uint32_t)buffer_.size(); }

	/* Slots kept free for RTSP control replies: media packets are refused
	   first, so a backlogged client still gets its keep-alive answers. */
	static const int kControlReserve = 32;

private:
	static const uint32_t kMaxHeaderSize = 16;

	typedef struct
	{
		uint8_t  header[kMaxHeaderSize];  /* per-connection bytes sent first */
		uint32_t header_size;
		std::shared_ptr<char> data;       /* sent after the header */
		uint32_t data_offset;
		uint32_t size;                    /* header_size + data length */
		uint32_t writeIndex;              /* bytes already sent, over header+data */
	} Packet;

	std::queue<Packet> buffer_;  		
	int max_queue_length_ = 0;
	 
	static const int kMaxQueueLength = 10000;
};

}

#endif


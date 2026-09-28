#ifndef XOP_MEMMORY_MANAGER_H
#define XOP_MEMMORY_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mutex>
#include <memory>
#include <queue>
#include <vector>

namespace xop
{

void* Alloc(uint32_t size);
void Free(void *ptr);

/* Returns a 1600-byte RTP buffer. Pooled buffers go back to the pool
   automatically when the last shared_ptr reference is released. */
std::shared_ptr<uint8_t> AllocRtpPacket();

class MemoryPool;
class RtpPacketPool;

struct MemoryBlock
{
	uint32_t block_id = 0;
	MemoryPool *pool = nullptr;
	MemoryBlock *next = nullptr;
};

class MemoryPool
{
public:
	MemoryPool();
	virtual ~MemoryPool();

	void  Init(uint32_t size, uint32_t n);
	void* Alloc(uint32_t size);
	void  Free(void* ptr);

	size_t BolckSize() const
	{ return block_size_; }

//private:
	char* memory_ = nullptr;
	uint32_t block_size_ = 0;
	uint32_t num_blocks_ = 0;
	MemoryBlock* head_ = nullptr;
	std::mutex mutex_;
};

static const uint32_t kRtpPacketBufferSize = 1600;

class RtpPacketPool
{
public:
	RtpPacketPool();
	virtual ~RtpPacketPool();

	void Init(uint32_t capacity);
	std::shared_ptr<uint8_t> AllocPacket();

	uint32_t GetAvailable()
	{ std::lock_guard<std::mutex> lock(mutex_); return (uint32_t)free_.size(); }

private:
	void Release(uint8_t *buffer);

	uint8_t *storage_ = nullptr;      /* capacity_ contiguous buffers */
	std::vector<uint8_t*> free_;      /* free list (LIFO, cache-friendly) */
	std::mutex mutex_;
	uint32_t capacity_ = 0;
};

class MemoryManager
{
public:
	static MemoryManager& Instance();
	~MemoryManager();

	void* Alloc(uint32_t size);
	void  Free(void* ptr);
	
	std::shared_ptr<uint8_t> AllocRtpPacket();

private:
	MemoryManager();

	static const int kMaxMemoryPool = 3;
	MemoryPool memory_pools_[kMaxMemoryPool];
	RtpPacketPool rtp_packet_pool_;
};

}
#endif

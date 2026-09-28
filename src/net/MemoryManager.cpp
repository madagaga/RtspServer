#include "MemoryManager.h"
#include "../xop/media.h"

using namespace xop;

void* xop::Alloc(uint32_t size)
{
	return MemoryManager::Instance().Alloc(size);
}

void xop::Free(void *ptr)
{
	return MemoryManager::Instance().Free(ptr);
}

std::shared_ptr<uint8_t> xop::AllocRtpPacket()
{
	return MemoryManager::Instance().AllocRtpPacket();
}

MemoryPool::MemoryPool()
{

}

MemoryPool::~MemoryPool()
{
	if (memory_) {
		free(memory_);
	}
}

void MemoryPool::Init(uint32_t size, uint32_t n)
{
	if (memory_) {
		return;
	}

	block_size_ = size;
	num_blocks_ = n;
	memory_ = (char*)malloc(num_blocks_ * (block_size_ + sizeof(MemoryBlock)));
	head_ = (MemoryBlock*)memory_;
	head_->block_id = 1;
	head_->pool = this;
	head_->next = nullptr;

	MemoryBlock* current = head_;
	for (uint32_t n = 1; n < num_blocks_; n++) {
		MemoryBlock* next = (MemoryBlock*)(memory_ + (n * (block_size_ + sizeof(MemoryBlock))));
		next->block_id = n + 1;
		next->pool = this;
		next->next = nullptr;

		current->next = next;
		current = next;
	}
}

void* MemoryPool::Alloc(uint32_t size)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (head_ != nullptr) {
		MemoryBlock* block = head_;
		head_ = head_->next;
		return ((char*)block + sizeof(MemoryBlock));
	}

	return nullptr;
}

void MemoryPool::Free(void* ptr)
{
	MemoryBlock *block = (MemoryBlock*)((char*)ptr - sizeof(MemoryBlock));
	if (block->block_id != 0) {
		std::lock_guard<std::mutex> locker(mutex_);
		block->next = head_;
		head_ = block;
	}
}

RtpPacketPool::RtpPacketPool()
{

}

RtpPacketPool::~RtpPacketPool()
{
	/* storage_ is intentionally not freed: packets still referenced by a
	   connection at exit would otherwise point to released memory. */
}

void RtpPacketPool::Init(uint32_t capacity)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (capacity_ > 0) {
		return; // Already initialized
	}

	storage_ = (uint8_t*)malloc(capacity * kRtpPacketBufferSize);
	if (storage_ == nullptr) {
		return; // Every AllocPacket() falls back to the heap
	}

	capacity_ = capacity;
	free_.reserve(capacity);
	for (uint32_t i = 0; i < capacity; i++) {
		free_.push_back(storage_ + i * kRtpPacketBufferSize);
	}
}

std::shared_ptr<uint8_t> RtpPacketPool::AllocPacket()
{
	uint8_t *buffer = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!free_.empty()) {
			buffer = free_.back();
			free_.pop_back();
		}
	}

	if (buffer != nullptr) {
		/* The deleter hands the buffer back to the pool once the last
		   reference (source, session or any client queue) is dropped. */
		return std::shared_ptr<uint8_t>(buffer, [this](uint8_t *p) { Release(p); });
	}

	/* Pool exhausted: no heap fallback, so the RTP footprint stays bounded
	   whatever the clients do. The caller drops the frame (and resyncs). */
	return nullptr;
}

void RtpPacketPool::Release(uint8_t *buffer)
{
	std::lock_guard<std::mutex> lock(mutex_);
	free_.push_back(buffer);
}

MemoryManager::MemoryManager()
{
	/* The generic memory_pools_ are not initialized: nothing calls
	   xop::Alloc(), and Init() touched ~1.1 MB up front. Alloc() then
	   falls back to malloc() if it is ever used. */
	rtp_packet_pool_.Init(RTP_PACKET_POOL_SIZE);
}

MemoryManager::~MemoryManager()
{

}

MemoryManager& MemoryManager::Instance()
{
	/* Never destroyed: pooled RTP buffers may be released after static
	   destructors have run (connections torn down at exit). */
	static MemoryManager *s_mgr = new MemoryManager();
	return *s_mgr;
}

void* MemoryManager::Alloc(uint32_t size)
{
	for (int n = 0; n < kMaxMemoryPool; n++) {
		if (size <= memory_pools_[n].BolckSize()) {
			void* ptr = memory_pools_[n].Alloc(size);
			if (ptr != nullptr) {
				return ptr;
			}				
			else {
				break;
			}
		}
	} 

	MemoryBlock *block = (MemoryBlock*)malloc(size + sizeof(MemoryBlock));
	block->block_id = 0;
	block->pool = nullptr;
	block->next = nullptr;
	return ((char*)block + sizeof(MemoryBlock));
}

void MemoryManager::Free(void* ptr)
{
	MemoryBlock *block = (MemoryBlock*)((char*)ptr - sizeof(MemoryBlock));
	MemoryPool *pool = block->pool;
	
	if (pool != nullptr && block->block_id > 0) {
		pool->Free(ptr);
	}
	else {
		::free(block);
	}
}

std::shared_ptr<uint8_t> MemoryManager::AllocRtpPacket()
{
	return rtp_packet_pool_.AllocPacket();
}
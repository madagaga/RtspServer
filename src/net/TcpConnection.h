#ifndef XOP_TCP_CONNECTION_H
#define XOP_TCP_CONNECTION_H

#include <atomic>
#include <mutex>
#include "TaskScheduler.h"
#include "BufferReader.h"
#include "BufferWriter.h"
#include "Channel.h"
#include "SocketUtil.h"

namespace xop
{

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
	using Ptr = std::shared_ptr<TcpConnection>;
	using DisconnectCallback = std::function<void(std::shared_ptr<TcpConnection> conn)> ;
	using CloseCallback = std::function<void(std::shared_ptr<TcpConnection> conn)>;
	using ReadCallback = std::function<bool(std::shared_ptr<TcpConnection> conn, xop::BufferReader& buffer)>;

	TcpConnection(TaskScheduler *task_scheduler, SOCKET sockfd);
	virtual ~TcpConnection();

	TaskScheduler* GetTaskScheduler() const 
	{ return task_scheduler_; }

	void SetReadCallback(const ReadCallback& cb)
	{ read_cb_ = cb; }

	void SetCloseCallback(const CloseCallback& cb)
	{ close_cb_ = cb; }

	void Send(std::shared_ptr<char> data, uint32_t size);
	void Send(const char *data, uint32_t size);
	/* Returns false when the packet was not queued (closed or queue full). */
	bool Send(const uint8_t *header, uint32_t header_size,
	          std::shared_ptr<uint8_t> payload, uint32_t payload_offset, uint32_t payload_size);
    
	void Disconnect();

	bool IsClosed() const 
	{ return is_closed_; }

	SOCKET GetSocket() const
	{ return channel_->GetSocket(); }

	uint16_t GetPort() const
	{ return SocketUtil::GetPeerPort(channel_->GetSocket()); }
    
	std::string GetIp() const
	{ return SocketUtil::GetPeerIp(channel_->GetSocket()); }

protected:
	friend class TcpServer;

	virtual void HandleRead();
	virtual void HandleWrite();
	virtual void HandleClose();
	virtual void HandleError();	

	void SetDisconnectCallback(const DisconnectCallback& cb)
	{ disconnect_cb_ = cb; }

	TaskScheduler* task_scheduler_;
	std::unique_ptr<xop::BufferReader> read_buffer_;
	std::unique_ptr<xop::BufferWriter> write_buffer_;
	std::atomic_bool is_closed_;

private:
	/* Bounded write queue: ~256 media packets (~400 KB, ~1.5 s at 2 Mbit/s)
	   plus BufferWriter::kControlReserve slots for RTSP replies. A stalled
	   peer can no longer grow the heap (was 50000 packets, ~80 MB). */
	static const int kMaxWriteQueue = 256 + BufferWriter::kControlReserve;

	void Close();

	std::shared_ptr<xop::Channel> channel_;
	std::mutex mutex_;
	DisconnectCallback disconnect_cb_;
	CloseCallback close_cb_;
	ReadCallback read_cb_;
};

}

#endif 

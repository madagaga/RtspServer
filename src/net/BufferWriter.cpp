// PHZ
// 2018-5-15

#include "BufferWriter.h"
#include "Socket.h"
#include "SocketUtil.h"
#if !defined(WIN32) && !defined(_WIN32)
#include <sys/uio.h>
#endif

using namespace xop;

void xop::WriteUint32BE(char* p, uint32_t value)
{
	p[0] = value >> 24;
	p[1] = value >> 16;
	p[2] = value >> 8;
	p[3] = value & 0xff;
}

void xop::WriteUint32LE(char* p, uint32_t value)
{
	p[0] = value & 0xff;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

void xop::WriteUint24BE(char* p, uint32_t value)
{
	p[0] = value >> 16;
	p[1] = value >> 8;
	p[2] = value & 0xff;
}

void xop::WriteUint24LE(char* p, uint32_t value)
{
	p[0] = value & 0xff;
	p[1] = value >> 8;
	p[2] = value >> 16;
}

void xop::WriteUint16BE(char* p, uint16_t value)
{
	p[0] = value >> 8;
	p[1] = value & 0xff;
}

void xop::WriteUint16LE(char* p, uint16_t value)
{
	p[0] = value & 0xff;
	p[1] = value >> 8;
}

BufferWriter::BufferWriter(int capacity) 
	: max_queue_length_(capacity)
{
	
}	

bool BufferWriter::Append(std::shared_ptr<char> data, uint32_t size, uint32_t index)
{
	if (size <= index) {
		return false;
	}
   
	if ((int)buffer_.size() >= max_queue_length_) {
		return false;
	}
     
	Packet pkt;
	pkt.header_size = 0;
	pkt.data = data;
	pkt.data_offset = 0;
	pkt.size = size;
	pkt.writeIndex = index;
	buffer_.emplace(std::move(pkt));
	return true;
}

bool BufferWriter::Append(const char* data, uint32_t size, uint32_t index)
{
	if (size <= index) {
		return false;
	}
     
	if ((int)buffer_.size() >= max_queue_length_) {
		return false;
	}
     
	Packet pkt;
	pkt.header_size = 0;
	pkt.data.reset(new char[size+512], std::default_delete<char[]>());
	memcpy(pkt.data.get(), data, size);
	pkt.data_offset = 0;
	pkt.size = size;
	pkt.writeIndex = index;
	buffer_.emplace(std::move(pkt));
	return true;
}

bool BufferWriter::Append(const uint8_t* header, uint32_t header_size,
                          std::shared_ptr<uint8_t> payload, uint32_t payload_offset, uint32_t payload_size)
{
	if (header_size > kMaxHeaderSize || header_size + payload_size == 0) {
		return false;
	}

	if ((int)buffer_.size() >= max_queue_length_ - kControlReserve) {
		return false;
	}

	Packet pkt;
	memcpy(pkt.header, header, header_size);
	pkt.header_size = header_size;
	pkt.data = std::shared_ptr<char>(payload, (char*)payload.get()); /* aliasing: shares ownership */
	pkt.data_offset = payload_offset;
	pkt.size = header_size + payload_size;
	pkt.writeIndex = 0;
	buffer_.emplace(std::move(pkt));
	return true;
}

int BufferWriter::Send(SOCKET sockfd, int timeout)
{		
	if (timeout > 0) {
		SocketUtil::SetBlock(sockfd, timeout); 
	}
      
	int ret = 0;
	int count = 1;

	do
	{
		if (buffer_.empty()) {
			return 0;
		}
		
		count -= 1;
		Packet &pkt = buffer_.front();
		char *data = pkt.data.get() + pkt.data_offset;
		if (pkt.writeIndex >= pkt.header_size) {
			uint32_t off = pkt.writeIndex - pkt.header_size;
			ret = ::send(sockfd, data + off, pkt.size - pkt.writeIndex, 0);
		}
		else {
#if !defined(WIN32) && !defined(_WIN32) /* not Windows */
			struct iovec iov[2];
			iov[0].iov_base = pkt.header + pkt.writeIndex;
			iov[0].iov_len  = pkt.header_size - pkt.writeIndex;
			iov[1].iov_base = data;
			iov[1].iov_len  = pkt.size - pkt.header_size;
			ret = ::writev(sockfd, iov, 2);
#else /* Windows: header first, payload on the next pass */
			ret = ::send(sockfd, (const char*)pkt.header + pkt.writeIndex, pkt.header_size - pkt.writeIndex, 0);
			if (ret > 0 && pkt.writeIndex + ret < pkt.size) {
				count += 1;
			}
#endif
		}
		if (ret > 0) {
			pkt.writeIndex += ret;
			if (pkt.size == pkt.writeIndex) {
				count += 1;
				buffer_.pop();
			}
		}
		else if (ret < 0) {
#if !defined(WIN32) && !defined(_WIN32) /* not Windows */
		if (errno == EINTR || errno == EAGAIN) 
#else /* Windows */
			int error = WSAGetLastError();
			if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == 0)
#endif
			{
				ret = 0;
			}
		}
	} while (count > 0);

	if (timeout > 0) {
		SocketUtil::SetNonBlock(sockfd);
	}
    
	return ret;
}



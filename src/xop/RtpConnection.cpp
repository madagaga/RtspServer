// PHZ
// 2018-9-30

#include "RtpConnection.h"
#include "RtspConnection.h"
#include "net/SocketUtil.h"
#if !defined(WIN32) && !defined(_WIN32)
#include <sys/uio.h>
#endif

using namespace std;
using namespace xop;

RtpConnection::RtpConnection(std::weak_ptr<TcpConnection> rtsp_connection)
    : rtsp_connection_(rtsp_connection)
{
	std::random_device rd;

	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		rtpfd_[chn] = 0;
		rtcpfd_[chn] = 0;
		memset(&media_channel_info_[chn], 0, sizeof(media_channel_info_[chn]));
		media_channel_info_[chn].rtp_header.version = RTP_VERSION;
		media_channel_info_[chn].packet_seq = rd()&0xffff;
		media_channel_info_[chn].rtp_header.seq = 0; //htons(1);
		media_channel_info_[chn].rtp_header.ts = htonl(rd());
		media_channel_info_[chn].rtp_header.ssrc = htonl(rd());
		resync_[chn] = false;
		nal_start_[chn] = true;
	}

	auto conn = rtsp_connection_.lock();
	rtsp_ip_ = conn->GetIp();
	rtsp_port_ = conn->GetPort();
}

RtpConnection::~RtpConnection()
{
	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		if(rtpfd_[chn] > 0) {
			SocketUtil::Close(rtpfd_[chn]);
		}

		if(rtcpfd_[chn] > 0) {
			SocketUtil::Close(rtcpfd_[chn]);
		}
	}
}

int RtpConnection::GetId() const
{
	auto conn = rtsp_connection_.lock();
	if (!conn) {
		return -1;
	}
	RtspConnection *rtspConn = (RtspConnection *)conn.get();
	return rtspConn->GetId();
}

bool RtpConnection::SetupRtpOverTcp(MediaChannelId channel_id, uint16_t rtp_channel, uint16_t rtcp_channel)
{
	auto conn = rtsp_connection_.lock();
	if (!conn) {
		return false;
	}

	media_channel_info_[channel_id].rtp_channel = rtp_channel;
	media_channel_info_[channel_id].rtcp_channel = rtcp_channel;
	rtpfd_[channel_id] = conn->GetSocket();
	rtcpfd_[channel_id] = conn->GetSocket();
	media_channel_info_[channel_id].is_setup = true;
	transport_mode_ = RTP_OVER_TCP;

	return true;
}

bool RtpConnection::SetupRtpOverUdp(MediaChannelId channel_id, uint16_t rtp_port, uint16_t rtcp_port)
{
	auto conn = rtsp_connection_.lock();
	if (!conn) {
		return false;
	}

	if(SocketUtil::GetPeerAddr(conn->GetSocket(), &peer_addr_) < 0) {
		return false;
	}

	media_channel_info_[channel_id].rtp_port = rtp_port;
	media_channel_info_[channel_id].rtcp_port = rtcp_port;

	std::random_device rd;
	for (int n = 0; n <= 10; n++) {
		if (n == 10) {
			return false;
		}
        
		local_rtp_port_[channel_id] = rd() & 0xfffe;
		local_rtcp_port_[channel_id] =local_rtp_port_[channel_id] + 1;

		rtpfd_[channel_id] = ::socket(AF_INET, SOCK_DGRAM, 0);
		if(!SocketUtil::Bind(rtpfd_[channel_id], "0.0.0.0",  local_rtp_port_[channel_id])) {
			SocketUtil::Close(rtpfd_[channel_id]);
			continue;
		}

		rtcpfd_[channel_id] = ::socket(AF_INET, SOCK_DGRAM, 0);
		if(!SocketUtil::Bind(rtcpfd_[channel_id], "0.0.0.0", local_rtcp_port_[channel_id])) {
			SocketUtil::Close(rtpfd_[channel_id]);
			SocketUtil::Close(rtcpfd_[channel_id]);
			continue;
		}

		break;
	}

	SocketUtil::SetSendBufSize(rtpfd_[channel_id], 200*1024);
  std::cerr << "SendBufSize is " << SocketUtil::GetSendBufSize(rtpfd_[channel_id]) << std::endl;

	peer_rtp_addr_[channel_id].sin_family = AF_INET;
	peer_rtp_addr_[channel_id].sin_addr.s_addr = peer_addr_.sin_addr.s_addr;
	peer_rtp_addr_[channel_id].sin_port = htons(media_channel_info_[channel_id].rtp_port);

	peer_rtcp_sddr_[channel_id].sin_family = AF_INET;
	peer_rtcp_sddr_[channel_id].sin_addr.s_addr = peer_addr_.sin_addr.s_addr;
	peer_rtcp_sddr_[channel_id].sin_port = htons(media_channel_info_[channel_id].rtcp_port);

	media_channel_info_[channel_id].is_setup = true;
	transport_mode_ = RTP_OVER_UDP;

	return true;
}

bool RtpConnection::SetupRtpOverMulticast(MediaChannelId channel_id, std::string ip, uint16_t port)
{
    std::random_device rd;
    for (int n = 0; n <= 10; n++) {
		if (n == 10) {
			return false;
		}
       
		local_rtp_port_[channel_id] = rd() & 0xfffe;
		rtpfd_[channel_id] = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (!SocketUtil::Bind(rtpfd_[channel_id], "0.0.0.0", local_rtp_port_[channel_id])) {
			SocketUtil::Close(rtpfd_[channel_id]);
			continue;
		}

		break;
    }

	media_channel_info_[channel_id].rtp_port = port;

	peer_rtp_addr_[channel_id].sin_family = AF_INET;
	peer_rtp_addr_[channel_id].sin_addr.s_addr = inet_addr(ip.c_str());
	peer_rtp_addr_[channel_id].sin_port = htons(port);

	media_channel_info_[channel_id].is_setup = true;
	transport_mode_ = RTP_OVER_MULTICAST;
	is_multicast_ = true;
	return true;
}

void RtpConnection::Play()
{
	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		if (media_channel_info_[chn].is_setup) {
			media_channel_info_[chn].is_play = true;
		}
	}
}

void RtpConnection::Record()
{
	for (int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		if (media_channel_info_[chn].is_setup) {
			media_channel_info_[chn].is_record = true;
			media_channel_info_[chn].is_play = true;
		}
	}
}

void RtpConnection::Teardown()
{
	if(!is_closed_) {
		is_closed_ = true;
		for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
			media_channel_info_[chn].is_play = false;
			media_channel_info_[chn].is_record = false;
		}
	}
}

string RtpConnection::GetMulticastIp(MediaChannelId channel_id) const
{
	return std::string(inet_ntoa(peer_rtp_addr_[channel_id].sin_addr));
}

string RtpConnection::GetRtpInfo(const std::string& rtsp_url)
{
	char buf[2048] = { 0 };
	snprintf(buf, 1024, "RTP-Info: ");

	int num_channel = 0;

	auto time_point = chrono::time_point_cast<chrono::milliseconds>(chrono::steady_clock::now());
	auto ts = time_point.time_since_epoch().count();
	for (int chn = 0; chn<MAX_MEDIA_CHANNEL; chn++) {
		uint32_t rtpTime = (uint32_t)(ts*media_channel_info_[chn].clock_rate / 1000);
		if (media_channel_info_[chn].is_setup) {
			if (num_channel != 0) {
				snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ",");
			}			

			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
					"url=%s/track%d;seq=0;rtptime=%u",
					rtsp_url.c_str(), chn, rtpTime);
			num_channel++;
		}
	}

	return std::string(buf);
}

void RtpConnection::SetFrameType(uint8_t frame_type)
{
	frame_type_ = frame_type;
	if(!has_key_frame_ && (frame_type == 0 || frame_type == VIDEO_FRAME_I)) {
		has_key_frame_ = true;
	}
}

/* header = RTP_TCP_HEAD_SIZE interleaved bytes + RTP_HEADER_SIZE RTP header.
   The packet buffer is shared by every client (and may still sit in another
   client's TCP queue), so the per-client header lives outside of it. */
void RtpConnection::BuildRtpHeader(MediaChannelId channel_id, const RtpPacket& pkt, uint8_t *header)
{
	media_channel_info_[channel_id].rtp_header.marker = pkt.last;
	media_channel_info_[channel_id].rtp_header.ts = htonl(pkt.timestamp);
	media_channel_info_[channel_id].rtp_header.seq = htons(media_channel_info_[channel_id].packet_seq++);

	header[0] = '$';
	header[1] = (uint8_t)media_channel_info_[channel_id].rtp_channel;
	header[2] = (uint8_t)(((pkt.size-4)&0xFF00)>>8);
	header[3] = (uint8_t)((pkt.size-4)&0xFF);
	memcpy(header+RTP_TCP_HEAD_SIZE, &media_channel_info_[channel_id].rtp_header, RTP_HEADER_SIZE);
}

int RtpConnection::SendRtpPacket(MediaChannelId channel_id, RtpPacket pkt)
{    
	if (is_closed_) {
		return -1;
	}
   
	auto conn = rtsp_connection_.lock();
	if (!conn) {
		return -1;
	}

	RtspConnection *rtsp_conn = (RtspConnection *)conn.get();

	bool ret = rtsp_conn->task_scheduler_->AddTriggerEvent([this, channel_id, pkt] {
		this->SetFrameType(pkt.type);

		bool at_nal_start = nal_start_[channel_id];
		nal_start_[channel_id] = (pkt.last != 0);
		if (resync_[channel_id]) {
			/* Same key-frame test as SetFrameType(), but only at a NAL boundary */
			if (!at_nal_start || !(pkt.type == 0 || pkt.type == VIDEO_FRAME_I)) {
				return;
			}
			resync_[channel_id] = false;
		}

		if((media_channel_info_[channel_id].is_play || media_channel_info_[channel_id].is_record) && has_key_frame_ ) {
			uint8_t header[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE];
			this->BuildRtpHeader(channel_id, pkt, header);
			if(transport_mode_ == RTP_OVER_TCP) {
				if (SendRtpOverTcp(channel_id, pkt, header) < 0) {
					this->DropPacket(channel_id, pkt.type);
				}
			}
			else {
				SendRtpOverUdp(channel_id, pkt, header);
			}

			//media_channel_info_[channel_id].octetCount  += pkt.size;
			//media_channel_info_[channel_id].packetCount += 1;
		}
	});

	if (!ret) {
		/* Event queue full: the loop thread never sees this packet */
		DropPacket(channel_id, pkt.type);
	}

	return ret ? 0 : -1;
}

void RtpConnection::DropPacket(MediaChannelId channel_id, uint8_t frame_type)
{
	/* A lost audio packet is a short glitch; a lost video packet corrupts
	   every frame up to the next key frame, so wait for it. */
	if (frame_type != AUDIO_FRAME) {
		resync_[channel_id] = true;
	}
}

int RtpConnection::SendRtpOverTcp(MediaChannelId channel_id, RtpPacket pkt, const uint8_t *header)
{
	auto conn = rtsp_connection_.lock();
	if (!conn) {
		return -1;
	}

	const uint32_t header_size = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE;
	if (!conn->Send(header, header_size, pkt.data, header_size, pkt.size - header_size)) {
		return -1;
	}
	return pkt.size;
}

int RtpConnection::SendRtpOverUdp(MediaChannelId channel_id, RtpPacket pkt, const uint8_t *header)
{
	const uint32_t payload_offset = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE;
#if !defined(WIN32) && !defined(_WIN32) /* not Windows */
	struct iovec iov[2];
	iov[0].iov_base = (void*)(header + RTP_TCP_HEAD_SIZE);
	iov[0].iov_len  = RTP_HEADER_SIZE;
	iov[1].iov_base = pkt.data.get() + payload_offset;
	iov[1].iov_len  = pkt.size - payload_offset;

	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_name    = &(peer_rtp_addr_[channel_id]);
	msg.msg_namelen = sizeof(struct sockaddr_in);
	msg.msg_iov     = iov;
	msg.msg_iovlen  = 2;
	int ret = sendmsg(rtpfd_[channel_id], &msg, 0);
#else /* Windows: assemble in a local buffer */
	char buf[1600];
	memcpy(buf, header + RTP_TCP_HEAD_SIZE, RTP_HEADER_SIZE);
	memcpy(buf + RTP_HEADER_SIZE, pkt.data.get() + payload_offset, pkt.size - payload_offset);
	int ret = sendto(rtpfd_[channel_id], buf, pkt.size - RTP_TCP_HEAD_SIZE, 0,
					(struct sockaddr *)&(peer_rtp_addr_[channel_id]), sizeof(struct sockaddr_in));
#endif
                   
	if(ret < 0) {        
		Teardown();
		return -1;
	}

	return ret;
}

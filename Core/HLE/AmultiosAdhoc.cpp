#include "AmultiosAdhoc.h"
bool RelayBuffer::ValidReadSize(std::size_t size_bytes) {
	return true;
}
void RelayBuffer::WriteBytes(const void * data, std::size_t size_bytes){
	if (data && (size_bytes > 0)) {
		std::size_t bsize = buffer.size();
		buffer.resize(bsize + size_bytes);
		std::memcpy(&buffer[bsize], data, size_bytes);
	}
}

void RelayBuffer::ReadBytes(void * object, std::size_t size_bytes) {
	if (object && ValidReadSize(size_bytes)) {
		std::memcpy(object, &buffer[rxpos], size_bytes);
		rxpos += size_bytes;
	}
}

//HLE UDP tunnel Creator
void AmultiosNetAdhocPdpCreate() {
};

void AmultiosNetAdhocPdpSend();

void AmultiosNetAdhocPdpRecv();

void AmultiosNetAdhocPdpDelete();

void AmultiosNetAdhocPtpClose();

void AmultiosNetAdhocPtpSend();

// HLE PTP Tunnel creator
void AmultiosNetAdhocPtpOpen();

void AmultiosNetAdhocPtpRecv();

void AmultiosNetAdhocPtpAccept();

void AmultiosNetAdhocPtpListen();

void AmultiosNetAdhocPtpConnect();

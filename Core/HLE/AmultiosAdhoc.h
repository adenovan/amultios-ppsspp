#pragma once
#include "proAdhoc.h"

// can be further used to handle serialization if needed by platform
class RelayBuffer {
public:
	bool ValidReadSize(std::size_t size);
	void WriteBytes(const void * data, std::size_t size_bytes);
	void ReadBytes(void * object, std::size_t size_bytes);
//	void WriteAdhocData(const std::vector<char>& data);
//	void ReadAdhocData(std::vector<char>& object);
	void ClearBuffer();
private:
	std::vector<uint8_t> buffer;
	std::size_t rxpos;
};

struct RelayLoginPacket {
	uint8_t opcode;
	SceNetEtherAddr mac;
	SceNetAdhocctlProductCode gamename;
};

struct RelayGroupConnectPacket {
	uint8_t opcode;
	SceNetAdhocctlGroupName groupname;
};

struct RelayGroupDisconnectPacket {
	uint8_t opcode;
};

struct RelayLogoutPacket {
	uint8_t opcode;
};

// Adhoc Data Message
struct AdhocMessage{
	SceNetEtherAddr sMac;
	uint16_t sPort;
	SceNetEtherAddr dstMac;
	uint16_t dstPort;
	std::vector<char> data;
};


// PDP Stat Emulated Over single socket
struct PDP_RELAY {
	SceNetAdhocPtpStat * pdpSocket;
	std::vector<AdhocMessage> incoming;
};

// PTP Stat Emulated Over single socket
struct PTP_RELAY {
	SceNetAdhocPtpStat * ptpSocket;
	std::vector<AdhocMessage> incoming;
};

#define TCP_RELAY_BACKLOG 1024;
#define OPCODE_RELAY_LOGIN 1
#define OPCODE_RELAY_CONNECT_GROUP 2
#define OPCODE_RELAY_DISCONNECT_GROUP 3
#define OPCODE_RELAY_LOGOUT 4
#define OPCODE_RELAY_DATA 5

// Relay Socket Client Side
class AmultiosRelay {
	void RelayStart();
	void RelayLoop();
	void RelayStop();
private:
	bool isConnected;
	bool isRunning;
	int relaySocket;
	RelayBuffer recvBuff;
};

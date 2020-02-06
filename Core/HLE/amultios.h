#include "Core/HLE/proAdhoc.h"


extern "C"
{
#include "mosquitto.h"
}

#define PIN_LENGTH 6

typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlNickname name;
	char pin[PIN_LENGTH];
	char revision[20];
} PACK AmultiosLoginPacket;

// Packet
typedef struct AmultiosMqtt
{
	std::string mqtt_id;
	struct mosquitto *mclient;
	std::string pub_topic;
	std::string sub_topic;
	std::string pub_topic_latest;
	size_t pub_payload_len_latest;
	std::string sub_topic_latest;
	int qos_latest;
	bool connected;
	bool ownThread;
	int subscribed;
	bool reconnectInProgress;
	bool disconnectComplete;
} AmultiosMqtt;

typedef struct PDPMessage
{
	std::string payload;
	int payloadlen;
	int sport;
	int dport;
	SceNetEtherAddr sourceMac;
	SceNetEtherAddr destinationMac;
} PDPMessage;

typedef struct PTPMessage
{
	std::string payload;
	int payloadlen;
	int sport;
	int dport;
	SceNetEtherAddr sourceMac;
	SceNetEtherAddr destinationMac;
} PTPMessage;

typedef struct LoginInfo
{
	std::string token;
	std::string mac;
	std::string nickname;
	std::string party;
	bool logedIn;
	bool authServer;
	bool pdpServer;
	bool ptpServer;
	bool ctlServer;
} LoginInfo;

#define PTP_AMULTIOS_CLOSED 0
#define PTP_AMULTIOS_OPEN 1
#define PTP_AMULTIOS_CONNECT 2
#define PTP_AMULTIOS_ACCEPT 3
#define PTP_AMULTIOS_LISTEN 4
#define PTP_AMULTIOS_ESTABLISHED 5

typedef struct PTPConnection
{
	s32_le id;
	uint8_t states;
	SceNetEtherAddr sourceMac;
	int sport;
	SceNetEtherAddr destinationMac;
	int dport;
} PTPConnection;

typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlGroupName group;
} PACK AmultiosNetAdhocctlConnectPacketC2S;

typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlNickname name;
} PACK AmultiosNetAdhocctlConnectPacketS2C;

typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlNickname name;
	SceNetAdhocctlProductCode game;
} PACK AmultiosNetAdhocctlLoginPacketS2C;

typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
} PACK AmultiosNetAdhocctlScanPacketC2S;

typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
} PACK AmultiosNetAdhocctlDisconnectPacketS2C;

//util
std::string getCurrentGroup();

void MqttTrace(void *level, char *message);
std::vector<std::string> explode(std::string const &s, char delim);
void getMac(SceNetEtherAddr *addr, std::string const &s);
std::string getMacString(const SceNetEtherAddr *addr);
bool isSameMAC(const SceNetEtherAddr *addr, const SceNetEtherAddr *addr2);

// library method
std::string getModeAddress();
void addAmultiosPeer(AmultiosNetAdhocctlConnectPacketS2C *packet);
void deleteAmultiosPeer(SceNetEtherAddr *mac);
bool macInNetwork(const SceNetEtherAddr *mac);

void amultios_login();
void amultios_sync();
int amultios_publish(const char *topic, void *payload, int size, int qos, unsigned long timeout);
int amultios_subscribe(const char *topic, int qos);
int amultios_unsubscribe(const char *topic);
void amultios_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void amultios_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void amultios_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void amultios_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void amultios_disconnect_callback(struct mosquitto *mosq, void *obj);
void amultios_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int ctl_publish(const char *topic, void *payload, int size, int qos, unsigned long timeout);
int ctl_subscribe(const char *topic, int qos);
int ctl_unsubscribe(const char *topic);
void ctl_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void ctl_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void ctl_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void ctl_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void ctl_disconnect_callback(struct mosquitto *mosq, void *obj);
void ctl_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int pdp_publish(const char *topic,void *payload, int size, int qos, unsigned long timeout);
int pdp_subscribe(const char *topic, int qos);
int pdp_unsubscribe(const char *topic);
void pdp_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void pdp_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void pdp_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void pdp_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void pdp_disconnect_callback(struct mosquitto *mosq, void *obj);
void pdp_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int ptp_publish(const char *topic, void *payload, int size, int qos, unsigned long timeout);
int ptp_subscribe(const char *topic, int qos);
int ptp_unsubscribe(const char *topic);
void ptp_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void ptp_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void ptp_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void ptp_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void ptp_disconnect_callback(struct mosquitto *mosq, void *obj);
void ptp_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int __AMULTIOS_INIT();
int __AMULTIOS_START();
int __AMULTIOS_SHUTDOWN();
int __AMULTIOS_CTL_INIT();
int __AMULTIIS_CTL_START();
int __AMULTIOS_CTL_SHUTDOWN();
int __AMULTIOS_PDP_INIT();
int __AMULTIOS_PDP_START();
int __AMULTIOS_PDP_SHUTDOWN();
int __AMULTIOS_PTP_INIT();
int __AMULTIOS_PTP_START();
int __AMULTIOS_PTP_SHUTDOWN();

//HLE FUNCTION
int AmultiosNetAdhocInit();
int AmultiosNetAdhocctlInit(SceNetAdhocctlAdhocId *adhoc_id);
int AmultiosNetAdhocctlScan();
int AmultiosNetAdhocctlCreate(const char *groupName);
int AmultiosNetAdhocctlDisconnect();
int AmultiosNetAdhocctlTerm();
int AmultiosNetAdhocTerm();

int AmultiosNetAdhocPdpCreate(const char *mac, u32 port, int bufferSize, u32 unknown);
int AmultiosNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag);
int AmultiosNetAdhocPdpRecv(int id, void *addr, void *port, void *buf, void *dataLength, u32 timeout, int flag);
int AmultiosNetAdhocPdpDelete(int id, int unknown);

int AmultiosNetAdhocPtpOpen(const char *srcmac, int sport, const char *dstmac, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int unknown);
int AmultiosNetAdhocPtpAccept(int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int flag);
int AmultiosNetAdhocPtpConnect(int id, int timeout, int flag);
int AmultiosNetAdhocPtpClose(int id, int unknown);
int AmultiosNetAdhocPtpListen(const char *srcmac, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, int unk);
int AmultiosNetAdhocPtpSend(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag);
int AmultiosNetAdhocPtpRecv(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag);

extern bool amultiosInited;
extern bool amultiosRunning;

extern bool ctlInited;
extern bool ctlRunning;

extern bool pdpInited;
extern bool pdpRunning;

extern bool ptpInited;
extern bool ptpRunning;
extern bool trusted;

class ChatMessages
{
public:
	typedef struct ChatMessage
	{
		std::string name;
		std::string room;
		std::string text;
		uint32_t namecolor;
		uint32_t roomcolor;
		uint32_t textcolor;
		std::string type;
	} ChatMessage;

	void Lock()
	{
		chatMutex_.lock();
	}

	void Unlock()
	{
		chatMutex_.unlock();
	}

	void setStatus(const std::string &status)
	{
		std::lock_guard<std::mutex> lk(playerStatusMutex);
		this->PlayerStatus = status;
		lastPlayerStatusUpdate = time_now();
		updatePlayerStatusFlag = true;
	};

	void listenPlayerStatus();
	void shutdownPlayerStatus();

	std::string getPlayerStatus()
	{
		std::lock_guard<std::mutex> lk(playerStatusMutex);
		return this->PlayerStatus;
	};
	void doPlayerStatusUpdate();
	bool getPlayerStatusUpdate() { return updatePlayerStatusFlag; };
	float getLastPlayerStatusUpdate() { return lastPlayerStatusUpdate; };

	void doChatUpdate();
	bool getChatUpdate() { return updateChatFlag; }
	void Update();
	float getLastUpdate();

	void doOSMUpdate()
	{
		std::lock_guard<std::mutex> locker(chatMutex_);
		updateOsmFlag = false;
	};

	bool getOSMUpdate() { return updateOsmFlag; }

	bool isChatScreenVisible();
	void toogleChatScreen(bool flag)
	{
		std::lock_guard<std::mutex> locker(chatMutex_);
		chatScreenVisible = flag;
		if (flag)
		{
			updateChatFlag = true;
			lastUpdate = time_now();
		}
	};

	bool isAdmin(const std::string &text);
	bool isMuted(const std::string &text);

	void ParseCommand(const std::string &command);
	void SendChat(const std::string &text);
	void Add(const std::string &text, const std::string &name = "Amultios", const std::string &room = "SYSTEM", const std::string &type = "TEXT", uint32_t namecolor = 0xF39621);

	std::list<ChatMessage> GetMessages()
	{
		std::lock_guard<std::mutex> lk(chatMutex_);
		std::list<ChatMessage> messages;
		auto room = this->SubcriptionList;
		std::copy_if(AllChatDb.begin(), AllChatDb.end(), std::back_inserter(messages), [&room](ChatMessage const &chat) { return std::find(room.begin(), room.end(), chat.room) != room.end(); });
		return messages;
	}

	void Clear()
	{
		std::lock_guard<std::mutex> lk(chatMutex_);
		this->AllChatDb.clear();
	}

private:
	std::string PlayerStatus;
	std::list<ChatMessage> AllChatDb;
	std::vector<std::string> MuteList;
	std::vector<std::string> displayMuteList;
	std::vector<std::string> ChannelList = {
		//National Official Languange based
		"WORLD",
		"BAHASA",
		"FILIPINO",
		"ENGLISH",
		"FRENCH",
		"ARABIC",
		"SPANISH",
		"PORTUGUESE",
		"GERMAN",
		"ITALIAN",
		"MALAY",
		"RUSSIAN",
		"JAPANESE",
		"KOREAN",
		"CHINESE",
		"DUTCH",
		"SWAHILI",
		"PERSIAN",
		"TAMIL",
		"QUECHUA",
		"IRISH",
		"ABKHAS"
		"KHMER",
		"MANDARIN",
		"CROATIAN",
		"CZECH",
		"SLOVAK",
		"DANISH",
		"TIGRINYA",
		"ESTONIAN",
		"FINNISH",
		"SWEDISH",
		"GREEK",
		"ICELANDIC",
		"HINDUSTANI",
		"PERSIAN",
		"HEBREW",
		"ROMANIAN",

		//country based
		"ALBANIA",
		"ALGERIA",
		"ANDORA",
		"ARMENIA",
		"AUSTRALIA",
		"AZERBAIJAN",
		"BANGLADESH",
		"BULGARIA",
		"BOSNIA",
		"CANADA",
		"CHINA",
		"CROATIA",
		"ETHIOPIA",
		"FINLAND",
		"FRANCE",
		"GERMANY",
		"HAITI",
		"ICELAND",
		"INDIA",
		"INDONESIA",
		"IRAN",
		"IRELAND",
		"ISRAEL",
		"ITALY",
		"JAPAN",
		"NORTHKOREA",
		"SOUTHKOREA",
		"KENYA",
		"LEBANON",
		"LUXEMBOURG",
		"MALTA",
		"MALAYSIA",
		"NAMIBIA",
		"NEPAL",
		"NIGERIA",
		"PAKISTAN",
		"POLAND",
		"PHILIPPINES",
		"ROMANIA",
		"RUSSIA",
		"SINGAPORE",
		"SERBIA",
		"SLOVENIA",
		"SOUTHAFRICA",
		"SPAIN",
		"SWITZERLAND",
		"TAIWAN",
		"THAILAND",
		"TUNISIA",
		"TURKEY",
		"UNITEDKINGDOM",
		"UGANDA",
		"UKRAINE",
		"UNITEDSTATES",
		"VIETNAM",

		//regional languange
		"JAWA",
		"TAGALOG",
	};
	std::vector<std::string> SubcriptionList = {"SYSTEM", "PRIVATE", "PARTY"};
	std::mutex chatMutex_;
	std::mutex playerStatusMutex;
	float lastUpdate;
	float lastPlayerStatusUpdate;
	bool chatScreenVisible;
	bool updateChatFlag;
	bool updatePlayerStatusFlag;
	bool updateOsmFlag;
	std::string selectedRoom = "WORLD";
	std::string selectedTopic = "WORLD";
	std::string preferedRoom = "PARTY";
	std::string privateMessageRoom = "";
	std::string privateMessageDisplay = "";
	std::vector<std::string> Admin = {"LUCIS", "AMULTIOS", "TINTIN", "ADENOVAN", "B4120NE", "GATOT", "RADIS3D"};
};

extern ChatMessages cmList;
extern LoginInfo loginInfo;

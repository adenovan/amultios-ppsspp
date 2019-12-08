#include "Core/HLE/proAdhoc.h"

extern "C"
{
#include "MQTTAsync.h"
}

// Packet
typedef struct AmultiosMqtt
{
  std::string mqtt_id;
  MQTTAsync client;
  std::string pub_topic;
  std::string sub_topic;
  std::string pub_topic_latest;
  size_t pub_payload_len_latest;
  std::string sub_topic_latest;
  int qos_latest;
  bool connected;
  int subscribed;
  bool reconnectInProgress;
} AmultiosMqtt;

typedef struct PDPMessage
{
  MQTTAsync_message * message = NULL;
  char * topicName = NULL;
  int topicLen;
  int sport;
  int dport;
  SceNetEtherAddr sourceMac;
  SceNetEtherAddr destinationMac;
} PDPMessage;

typedef struct PTPMessage
{
  MQTTAsync_message *message = NULL;
  char *topicName = NULL;
  int topicLen;
  int sport;
  int dport;
  SceNetEtherAddr sourceMac;
  SceNetEtherAddr destinationMac;
} PTPMessage;


#define PTP_AMULTIOS_CLOSED 0
#define PTP_AMULTIOS_OPEN 1
#define PTP_AMULTIOS_CONNECT 2
#define PTP_AMULTIOS_ACCEPT 3
#define PTP_AMULTIOS_LISTEN 4
#define PTP_AMULTIOS_ESTABLISHED 5

typedef struct PTPConnection
{
  uint8_t states;
  SceNetEtherAddr sourceMac;
  int sport;
  SceNetEtherAddr destinationMac;
  int dport;
} PTPConnection;

typedef struct PTPTopic
{
  s32_le states;
  std::string sub_topic;
  std::string open_topic;
  std::string accept_topic;
  std::string connect_topic;
  std::string pub_topic;
  SceNetEtherAddr sourceMac;
  int sport;
  SceNetEtherAddr destinationMac;
  int dport;
  std::vector<PTPConnection> backlog;
} PTPTopic;

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
} PACK AmultiosNetAdhocctlScanPacketC2S;

typedef struct
{
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
} PACK AmultiosNetAdhocctlDisconnectPacketS2C;

//util
std::vector<std::string> explode(std::string const &s, char delim);
void getMac(SceNetEtherAddr *addr, std::string const &s);
std::string getMacString(SceNetEtherAddr *addr);
bool isSameMAC(const SceNetEtherAddr *addr, const SceNetEtherAddr *addr2);

// library method
void addAmultiosPeer(AmultiosNetAdhocctlConnectPacketS2C *packet);
void deleteAmultiosPeer(SceNetEtherAddr *mac);
bool macInNetwork(SceNetEtherAddr *mac);

int ctl_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int ctl_subscribe(const char *topic, int qos);
int ctl_unsubscribe(const char *topic);
void ctl_publish_success(void *context, MQTTAsync_successData *response);
void ctl_publish_failure(void *context, MQTTAsync_failureData *response);
void ctl_subscribe_success(void *context, MQTTAsync_successData *response);
void ctl_subscribe_failure(void *context, MQTTAsync_failureData *response);
void ctl_unsubscribe_success(void *context, MQTTAsync_successData *response);
void ctl_unsubscribe_failure(void *context, MQTTAsync_failureData *response);
void ctl_connect_success(void *context, MQTTAsync_successData *response);
void ctl_connect_failure(void *context, MQTTAsync_failureData *response);
void ctl_disconnect_success(void *context, MQTTAsync_successData *response);
void ctl_disconnect_failure(void *context, MQTTAsync_failureData *response);
void ctl_connect_lost(void *context, char *cause);
int ctl_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);

int pdp_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int pdp_subscribe(const char *topic, int qos);
int pdp_unsubscribe(const char *topic);
void pdp_publish_success(void *context, MQTTAsync_successData *response);
void pdp_publish_failure(void *context, MQTTAsync_failureData *response);
void pdp_subscribe_success(void *context, MQTTAsync_successData *response);
void pdp_subscribe_failure(void *context, MQTTAsync_failureData *response);
void pdp_unsubscribe_success(void *context, MQTTAsync_successData *response);
void pdp_unsubscribe_failure(void *context, MQTTAsync_failureData *response);
void pdp_connect_success(void *context, MQTTAsync_successData *response);
void pdp_connect_failure(void *context, MQTTAsync_failureData *response);
void pdp_disconnect_success(void *context, MQTTAsync_successData *response);
void pdp_disconnect_failure(void *context, MQTTAsync_failureData *response);
void pdp_connect_lost(void *context, char *cause);
int pdp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);


int ptp_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int ptp_subscribe(const char *topic, int qos);
int ptp_unsubscribe(const char *topic);
void ptp_publish_success(void *context, MQTTAsync_successData *response);
void ptp_publish_failure(void *context, MQTTAsync_failureData *response);
void ptp_subscribe_success(void *context, MQTTAsync_successData *response);
void ptp_subscribe_failure(void *context, MQTTAsync_failureData *response);
void ptp_unsubscribe_success(void *context, MQTTAsync_successData *response);
void ptp_unsubscribe_failure(void *context, MQTTAsync_failureData *response);
void ptp_connect_success(void *context, MQTTAsync_successData *response);
void ptp_connect_failure(void *context, MQTTAsync_failureData *response);
void ptp_disconnect_success(void *context, MQTTAsync_successData *response);
void ptp_disconnect_failure(void *context, MQTTAsync_failureData *response);
void ptp_connect_lost(void *context, char *cause);
int ptp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);

int __AMULTIOS_CTL_INIT();
int __AMULTIOS_CTL_SHUTDOWN();
int __AMULTIOS_PDP_INIT();
int __AMULTIOS_PDP_SHUTDOWN();
int __AMULTIOS_PTP_INIT();
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

extern bool ctlInited;
extern bool ctlRunning;
extern std::mutex ctl_running_mutex;
extern std::condition_variable ctl_running_cv;
extern std::thread ctlThread;

extern bool pdpInited;
extern bool pdpRunning;
extern std::mutex pdp_running_mutex;
extern std::condition_variable pdp_running_cv;
extern std::thread pdpThread;

extern bool ptpInited;
extern bool ptpRunning;
extern std::mutex ptp_running_mutex;
extern std::condition_variable ptp_running_cv;
extern std::thread ptpThread;

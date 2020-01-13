#include "Core/HLE/proAdhoc.h"

extern "C"
{
#include <mosquitto.h>
}

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
  std::vector<char> payload;
  int payloadlen;
  int sport;
  int dport;
  SceNetEtherAddr sourceMac;
  SceNetEtherAddr destinationMac;
} PDPMessage;

typedef struct PTPMessage
{
  std::vector<char> payload;
  int payloadlen;
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

int amultios_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int amultios_subscribe(const char *topic, int qos);
int amultios_unsubscribe(const char *topic);
void amultios_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void amultios_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void amultios_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void amultios_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void amultios_disconnect_callback(struct mosquitto *mosq, void *obj);
void amultios_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int ctl_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int ctl_subscribe(const char *topic, int qos);
int ctl_unsubscribe(const char *topic);
void ctl_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void ctl_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void ctl_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void ctl_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void ctl_disconnect_callback(struct mosquitto *mosq, void *obj);
void ctl_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int pdp_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int pdp_subscribe(const char *topic, int qos);
int pdp_unsubscribe(const char *topic);
void pdp_publish_callback(struct mosquitto *mosq, void *obj, int mid);
void pdp_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
void pdp_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid);
void pdp_connect_callback(struct mosquitto *mosq, void *obj, int rc);
void pdp_disconnect_callback(struct mosquitto *mosq, void *obj);
void pdp_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

int ptp_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
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

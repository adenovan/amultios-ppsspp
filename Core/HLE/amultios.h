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
  int port;
  unsigned long timeout;
  bool connected;
  bool subscribed;
} AmultiosMqtt;

typedef struct PDPMessage
{
  MQTTAsync_message * message = NULL;
  char * topicName = NULL;
  int topicLen;
  int port;
  SceNetEtherAddr sourceMac;
  SceNetEtherAddr destinationMac;
} PDPMessage;

typedef struct PTPMessage
{
  MQTTAsync_message *message = NULL;
  char *topicName = NULL;
  int topicLen;
  int port;
  SceNetEtherAddr sourceMac;
  SceNetEtherAddr destinationMac;
} PTPMessage;

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

// library method
int publish(AmultiosMqtt *amultios_mqtt, const char *topic, void *payload, size_t size, int qos, unsigned long timeout);
int subscribe(AmultiosMqtt *amultios_mqtt, const char *topic, int qos);
int unsubscribe(AmultiosMqtt *amultios_mqtt, const char *topic);
void addAmultiosPeer(AmultiosNetAdhocctlConnectPacketS2C *packet);
void deleteAmultiosPeer(SceNetEtherAddr *mac);
bool macInNetwork(SceNetEtherAddr *mac);

void publish_success(void *context, MQTTAsync_successData *response);
void publish_failure(void *context, MQTTAsync_failureData *response);

void subscribe_success(void *context, MQTTAsync_successData *response);
void subscribe_failure(void *context, MQTTAsync_failureData *response);

void unsubscribe_success(void *context, MQTTAsync_successData *response);
void unsubscribe_failure(void *context, MQTTAsync_failureData *response);

void ctl_connect_success(void *context, MQTTAsync_successData *response);
void ctl_connect_failure(void *context, MQTTAsync_failureData *response);
void ctl_disconnect_success(void *context, MQTTAsync_successData *response);
void ctl_connect_lost(void *context, char *cause);
int ctl_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);

void pdp_connect_success(void *context, MQTTAsync_successData *response);
void pdp_connect_failure(void *context, MQTTAsync_failureData *response);
void pdp_disconnect_success(void *context, MQTTAsync_successData *response);
void pdp_disconnect_failure(void *context, MQTTAsync_failureData *response);
void pdp_connect_lost(void *context, char *cause);
int pdp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);

void ptp_connect_success(void *context, MQTTAsync_successData *response);
void ptp_connect_failure(void *context, MQTTAsync_failureData *response);
void ptp_disconnect_success(void *context, MQTTAsync_successData *response);
void ptp_disconnect_failure(void *context, MQTTAsync_failureData *response);
void ptp_connect_lost(void *context, char * cause);
int ptp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);

int __AMULTIOS_CTL_INIT();
int __AMULTIOS_CTL_SHUTDOWN();

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

extern bool ctlRunning;
extern std::thread ctlThread;
extern std::mutex ctl_mutex;
extern AmultiosMqtt * ctl_mqtt;

extern bool pdpRunning;
extern AmultiosMqtt * pdp_mqtt;
extern std::thread pdpThread;

extern bool ptpRunning;
extern AmultiosMqtt * ptp_mqtt;
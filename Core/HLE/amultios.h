#include "Core/HLE/proAdhoc.h"
#include "MQTTClient.h"

// Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
  SceNetAdhocctlGroupName group;
} PACK AmultiosNetAdhocctlConnectPacketC2S;

typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
  SceNetAdhocctlNickname name;
} PACK AmultiosNetAdhocctlConnectPacketS2C;

typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
} PACK AmultiosNetAdhocctlScanPacketC2S;

typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
} PACK AmultiosNetAdhocctlDisconnectPacketS2C;

// library method
void delivered(void *context, MQTTClient_deliveryToken dt);
void connlost(void *context, char *cause);
int publish(const char * topic, void * payload,size_t size, int qos);
int publish_wait(const char *topic, void *payload, size_t size, int qos,unsigned long timeout);
int subscribe(const char * topic, int qos);
int unsubscribe(const char * topic, int qos);
void addAmultiosPeer(AmultiosNetAdhocctlConnectPacketS2C *packet);
void deleteAmultiosPeer(SceNetEtherAddr *mac);
bool macInNetwork(SceNetEtherAddr * mac);

int ctl_run();

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
extern bool clientConnected;
extern bool ctlRunning;
extern std::thread ctlThread;
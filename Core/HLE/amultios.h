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
} PACK AmultiosNetAdhocctlScanPacketC2S;

typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
} PACK AmultiosNetAdhocctlDisconnectPacketS2C;

// library method
void delivered(void *context, MQTTClient_deliveryToken dt);
void connlost(void *context, char *cause);
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message);
int publish(const char * topic, void * payload,size_t size, int qos);
int subscribe(const char * topic, int qos);
int unsubscribe(const char * topic, int qos);

int ctl_run();

//HLE FUNCTION
int AmultiosNetAdhocInit();
int AmultiosNetAdhocctlInit(SceNetAdhocctlAdhocId *adhoc_id);
int AmultiosNetAdhocctlScan();
int AmultiosNetAdhocctlCreate(const char *groupName);
int AmultiosNetAdhocctlTerm();
int AmultiosNetAdhocTerm();

extern bool clientConnected;
extern bool ctlRunning;
extern std::thread ctlThread;
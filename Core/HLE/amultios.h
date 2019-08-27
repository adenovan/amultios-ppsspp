#include "Core/HLE/proAdhoc.h"
#include "MQTTClient.h"

void delivered(void *context, MQTTClient_deliveryToken dt);
void connlost(void *context, char *cause);
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message);
int publish(const char * topic, void * payload,size_t size, int qos);
int subscribe(const char * topic, int qos);
int unsubscribe(const char * topic, int qos);
int connectAmultios();
int disconnectAmultios();
int ctl_run();
int initSceNetAdhocctl(SceNetAdhocctlAdhocId *adhoc_id);
extern bool clientConnected;
extern bool ctlRunning;
extern std::thread ctlThread;
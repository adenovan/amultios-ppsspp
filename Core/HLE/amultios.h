#include "Core/HLE/proAdhoc.h";
#include "MQTTClient.h"

int connectAmultios();
int disconnectAmultios();
int publish(const char * topic, const char * payload, const int qos);

extern bool clientConnected;
/*******************************************************************************
 * Copyright (c) 2012, 2017 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *   http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial contribution
 *******************************************************************************/

#include "Core/Core.h"
#include "Core/Host.h"
#include "i18n/i18n.h"
#include "base/timeutil.h"
#include "amultios.h"

#define ADDRESS "tcp://amultios.net:1883"
// #define TOPIC       "SceNetAdhocctl"
// #define PAYLOAD     "Hello World!"
// #define QOS         1
#define TIMEOUT 10000L

struct my_context {
  int foo;
};
bool clientConnected = false;
bool ctlRunning = false;
std::thread ctlThread;
MQTTClient clientSocket;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    INFO_LOG(SCENET, "Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

void connlost(void *context, char *cause)
{
    INFO_LOG(SCENET, "MQTT Connection Lost cause %s", cause);
}

int msgarrvd(void *context, char * topicName, int topicLen, MQTTClient_message *message)
{
    NOTICE_LOG(SCENET, "Topic[%s] message %s len %d", topicName, message->payload, message->payloadlen);

    if(strcmp(topicName,"SceNetAdhocctl") == 0){
        
    }
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int publish(const char *topic, void *payload, size_t size, int qos)
{
    int success = MQTTCLIENT_FAILURE;
    if (clientConnected)
    {
        MQTTClient_message msg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        msg.payload = payload;
        msg.payloadlen = (int)size;
        msg.qos = qos;
        msg.retained = 0;
        MQTTClient_publishMessage(clientSocket, topic, &msg, &token);
        success = MQTTClient_waitForCompletion(clientSocket, token, TIMEOUT);
        //NOTICE_LOG(SCENET, "Message %s with delivery token %d delivered\n", (char *)payload, token);
    }
    return success;
}

int subscribe(const char *topic, int qos)
{
    if (clientConnected)
    {
        return MQTTClient_subscribe(clientSocket, topic, qos);
    }
    return -1;
}

int unsubscribe(const char *topic, int qos)
{
    if (clientConnected)
    {
       return MQTTClient_unsubscribe(clientSocket, topic);
    }
    return -1;
}

int connectAmultios()
{

    int rc;

    MQTTClient_create(&clientSocket, ADDRESS, g_Config.sNickName.c_str(),
                      MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    if(( rc = MQTTClient_setCallbacks(clientSocket, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS){
        ERROR_LOG(SCENET, "Failed to set callback, return code %d\n", rc);
    };

    if ((rc = MQTTClient_connect(clientSocket, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        ERROR_LOG(SCENET, "Failed to connect, return code %d\n", rc);
        clientConnected = false;
    }
    clientConnected = true;
    subscribe("SceNetAdhocctl",2);

    NOTICE_LOG(SCENET, "Mqtt client connected , code %d", rc);
    return rc;
}

int disconnectAmultios()
{
    if (clientConnected)
    {
        MQTTClient_disconnect(clientSocket, 10000);
        MQTTClient_destroy(&clientSocket);
        clientConnected = false;
        NOTICE_LOG(SCENET, "Mqtt client Disconnected");
        return 0;
    }
    return -1;
}

int initSceNetAdhocctl(SceNetAdhocctlAdhocId *adhoc_id)
{
    SceNetAdhocctlLoginPacketC2S packet;
    packet.base.opcode = OPCODE_LOGIN;
    SceNetEtherAddr addres;
    getLocalMac(&addres);
    packet.mac = addres;
    strcpy((char *)packet.name.data, g_Config.sNickName.c_str());
    memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);
    return publish("SceNetAdhocctl", &packet, sizeof(packet), 2);
}

int ctl_run(){
    NOTICE_LOG(SCENET,"Begin of ctl thread");
    while(ctlRunning && clientConnected){
        MQTTClient_yield();
    }
    NOTICE_LOG(SCENET,"End of ctl thread");
    return 0;
}
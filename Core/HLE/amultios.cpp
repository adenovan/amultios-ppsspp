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

#define ADDRESS     "tcp://amultios.net:1883"
// #define TOPIC       "SceNetAdhocctl"
// #define PAYLOAD     "Hello World!"
// #define QOS         1
#define TIMEOUT     10000L

bool clientConnected = false;
MQTTClient clientSocket;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

int publish(const char * topic, const char * payload, const int qos){
    int success = 1;
    if(clientConnected){
        MQTTClient_message msg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        msg.payload = (char *)payload;
        msg.payloadlen = (int)strlen(payload);
        msg.qos = qos;
        msg.retained = 0;
        MQTTClient_publishMessage(clientSocket, topic, &msg, &token);
        success = MQTTClient_waitForCompletion(clientSocket, token, TIMEOUT);
        NOTICE_LOG(SCENET,"Message with delivery token %d delivered\n", token);
    }
    return success;
}

int connectAmultios()
{
    MQTTClient client;
    int rc;

    MQTTClient_create(&client, ADDRESS, g_Config.sNickName.c_str(),
        MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        ERROR_LOG(SCENET,"Failed to connect, return code %d\n", rc);
        clientConnected = false;
    }
   
    clientConnected = true;

    NOTICE_LOG(SCENET,"Mqtt client connected , code %d",rc);
    return rc;
}

int disconnectAmultios(){
    if(clientConnected){
        MQTTClient_disconnect(clientSocket, 10000);
        MQTTClient_destroy(&clientSocket);
        clientConnected = false;
    }
}
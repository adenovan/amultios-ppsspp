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
// #define TOPIC       "AMULTIOSAdhocctl"
// #define PAYLOAD     "Hello World!"
// #define QOS         1
#define TIMEOUT 10000L

struct my_context
{
    int foo;
};

char ctl_self_topic[28];
bool clientConnected = false;
bool ctlRunning = false;
std::thread ctlThread;
MQTTClient clientSocket;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
MQTTClient_willOptions ctl_will = MQTTClient_willOptions_initializer;

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    INFO_LOG(AMULTIOS, "Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

void connlost(void *context, char *cause)
{
    INFO_LOG(AMULTIOS, "MQTT Connection Lost cause %s", cause);
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    NOTICE_LOG(AMULTIOS, "Topic[%s] message %s len %d", topicName, message->payload, message->payloadlen);
    char *payload_ptr = static_cast<char *>(message->payload);
    if (strcmp(topicName, ctl_self_topic) == 0)
    {
        if (payload_ptr[0] == OPCODE_CONNECT_BSSID)
        {
            NOTICE_LOG(AMULTIOS, "[CTL_NETWORK]Incoming Opcode connect BSSID");
            // Cast Packet
            SceNetAdhocctlConnectBSSIDPacketS2C *packet = (SceNetAdhocctlConnectBSSIDPacketS2C *)payload_ptr;
            // Update BSSID
            parameter.bssid.mac_addr = packet->mac;
            // Change State
            threadStatus = ADHOCCTL_STATE_CONNECTED;
            // Notify Event Handlers
            notifyAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0);
        }
        else if (payload_ptr[0] == OPCODE_SCAN)
        {
            // Log Incoming Network Information
            INFO_LOG(SCENET, "[CTL_NETWORK] Incoming Group Information...");
            // Cast Packet
            SceNetAdhocctlScanPacketS2C *packet = (SceNetAdhocctlScanPacketS2C *)payload_ptr;

            // Multithreading Lock
            peerlock.lock();

            // Should only add non-existing group (or replace an existing group) to prevent Ford Street Racing from showing a strange game session list
            SceNetAdhocctlScanInfo *group = findGroup(&packet->mac);

            if (group != NULL)
            {
                // Copy Group Name
                group->group_name = packet->group;

                // Set Group Host
                group->bssid.mac_addr = packet->mac;
            }
            else
            {
                // Allocate Structure Data
                SceNetAdhocctlScanInfo *group = (SceNetAdhocctlScanInfo *)malloc(sizeof(SceNetAdhocctlScanInfo));

                // Allocated Structure Data
                if (group != NULL)
                {
                    // Clear Memory, should this be done only when allocating new group?
                    memset(group, 0, sizeof(SceNetAdhocctlScanInfo));

                    // Link to existing Groups
                    group->next = newnetworks;

                    // Copy Group Name
                    group->group_name = packet->group;

                    // Set Group Host
                    group->bssid.mac_addr = packet->mac;

                    // Link into Group List
                    newnetworks = group;
                }
            }

            // Multithreading Unlock
            peerlock.unlock();
        }
        else if (payload_ptr[0] == OPCODE_SCAN_COMPLETE)
        {
            NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] Incoming scan complete packet");

            // Reset current networks to prevent leaving host to be listed again
            peerlock.lock();
            freeGroupsRecursive(networks);
            networks = newnetworks;
            newnetworks = NULL;
            peerlock.unlock();

            // Change State
            threadStatus = ADHOCCTL_STATE_DISCONNECTED;

            // Notify Event Handlers
            notifyAdhocctlHandlers(ADHOCCTL_EVENT_SCAN, 0);
        }
        else if (payload_ptr[0] == OPCODE_CONNECT)
        {
            NOTICE_LOG(AMULTIOS, "GOT opcode Connect");
            
        }
        else if (payload_ptr[0] == OPCODE_DISCONNECT)
        {
            NOTICE_LOG(AMULTIOS, "GOT opcode disconnect");
        }
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
        success = MQTTClient_publishMessage(clientSocket, topic, &msg, &token);
        if (qos > 0)
        {
            MQTTClient_waitForCompletion(clientSocket, token, TIMEOUT);
        }
        //NOTICE_LOG(AMULTIOS, "Message %s with delivery token %d delivered\n", (char *)payload, token);
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

int ctl_run()
{
    NOTICE_LOG(AMULTIOS, "Begin of ctl thread");
    while (ctlRunning && clientConnected)
    {
        MQTTClient_yield();
    }

    NOTICE_LOG(AMULTIOS, "End of ctl thread");
    AmultiosNetAdhocctlTerm();
    return 0;
}

int AmultiosNetAdhocInit()
{

    int rc;
    MQTTClient_create(&clientSocket, ADDRESS, g_Config.sNickName.c_str(), MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    AmultiosNetAdhocctlDisconnectPacketS2C packet;
    packet.base.opcode = OPCODE_DISCONNECT;
    SceNetEtherAddr addres;
    getLocalMac(&addres);
    packet.mac = addres;

    ctl_will.message = (char *)&packet;
    ctl_will.topicName = "SceNetAdhocctl";
    ctl_will.qos = 2;
    ctl_will.retained = 0;
    conn_opts.will = &ctl_will;
    if ((rc = MQTTClient_setCallbacks(clientSocket, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS)
    {
        ERROR_LOG(AMULTIOS, "Failed to set callback, return code %d\n", rc);
    };

    if ((rc = MQTTClient_connect(clientSocket, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        ERROR_LOG(AMULTIOS, "Failed to connect, return code %d\n", rc);
        clientConnected = false;
    }
    clientConnected = true;
    snprintf(ctl_self_topic, sizeof(ctl_self_topic), "%02x%02x%02x%02x%02x%02x%s",
             addres.data[0], addres.data[1], addres.data[2], addres.data[3], addres.data[4], addres.data[5], "/SceNetAdhocctl");
    NOTICE_LOG(AMULTIOS, "MQTT Subscribe to %s", ctl_self_topic);
    subscribe(ctl_self_topic, 2);
    NOTICE_LOG(AMULTIOS, "Mqtt client connected , code %d", rc);
    return rc;
}

int AmultiosNetAdhocctlInit(SceNetAdhocctlAdhocId *adhoc_id)
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

int AmultiosNetAdhocctlCreate(const char *groupName)
{
    const SceNetAdhocctlGroupName *groupNameStruct = (const SceNetAdhocctlGroupName *)groupName;
    // Library initialized
    if (netAdhocctlInited)
    {
        // Valid Argument
        if (validNetworkName(groupNameStruct))
        {
            // Disconnected State, may also need to check for Scanning state to prevent some games from failing to host a game session
            if ((threadStatus == ADHOCCTL_STATE_DISCONNECTED) || (threadStatus == ADHOCCTL_STATE_SCANNING))
            {
                // Set Network Name
                if (groupNameStruct != NULL)
                    parameter.group_name = *groupNameStruct;

                // Reset Network Name
                else
                    memset(&parameter.group_name, 0, sizeof(parameter.group_name));

                // Prepare Connect Packet
                AmultiosNetAdhocctlConnectPacketC2S packet;

                // Clear Packet Memory
                memset(&packet, 0, sizeof(packet));

                // Set Packet Opcode
                packet.base.opcode = OPCODE_CONNECT;
                // Set Target Group
                if (groupNameStruct != NULL)
                    packet.group = *groupNameStruct;

                SceNetEtherAddr addres;
                getLocalMac(&addres);
                packet.mac = addres;
                // Acquire Network Lock

                int iResult = publish("SceNetAdhocctl", &packet, sizeof(packet), 2);

                if (iResult != MQTTCLIENT_SUCCESS)
                {
                    ERROR_LOG(AMULTIOS, "Mqtt Error when sending reason %d", iResult);
                    threadStatus = ADHOCCTL_STATE_CONNECTED;
                }

                // Free Network Lock

                // Set HUD Connection Status
                //setConnectionStatus(1);

                // Wait for Status to be connected to prevent Ford Street Racing from Failed to create game session
                // if (friendFinderRunning) {
                // 	int cnt = 0;
                // 	while ((threadStatus != ADHOCCTL_STATE_CONNECTED) && (cnt < 5000)) {
                // 		sleep_ms(1);
                // 		cnt++;
                // 	}
                // }

                // Return Success
                return 0;
            }

            // Connected State
            return ERROR_NET_ADHOCCTL_BUSY; // ERROR_NET_ADHOCCTL_BUSY may trigger the game (ie. Ford Street Racing) to call sceNetAdhocctlDisconnect
        }

        // Invalid Argument
        return ERROR_NET_ADHOC_INVALID_ARG;
    }
    // Library uninitialized
    return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int AmultiosNetAdhocctlScan()
{
    // Library initialized
    if (netAdhocctlInited && clientConnected)
    {
        // Not connected
        if (threadStatus == ADHOCCTL_STATE_DISCONNECTED)
        {
            threadStatus = ADHOCCTL_STATE_SCANNING;

            // Prepare Scan Request Packet

            AmultiosNetAdhocctlScanPacketC2S packet;

            // Clear Packet Memory
            memset(&packet, 0, sizeof(packet));

            // Set Packet Opcode
            packet.base.opcode = OPCODE_SCAN;

            SceNetEtherAddr addres;
            getLocalMac(&addres);
            packet.mac = addres;

            // Send Scan Request Packet, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
            int iResult = publish("SceNetAdhocctl", &packet, sizeof(packet), 2);

            if (iResult != MQTTCLIENT_SUCCESS)
            {
                ERROR_LOG(AMULTIOS, "Mqtt Error when sending scan reason %d", iResult);
                threadStatus = ADHOCCTL_STATE_DISCONNECTED;
                //if (error == ECONNABORTED || error == ECONNRESET || error == ENOTCONN) return ERROR_NET_ADHOCCTL_NOT_INITIALIZED; // A case where it need to reconnect to AdhocServer
                return ERROR_NET_ADHOCCTL_DISCONNECTED; // ERROR_NET_ADHOCCTL_BUSY
            }
            // Return Success
            return 0;
        }

        // Library is busy
        return ERROR_NET_ADHOCCTL_BUSY; // ERROR_NET_ADHOCCTL_BUSY may trigger the game (ie. Ford Street Racing) to call sceNetAdhocctlDisconnect
    }

    // Library uninitialized
    return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}


int AmultiosNetAdhocctlTerm(){
    if(clientConnected){
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_DISCONNECT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        int iResult = publish("SceNetAdhocctl", &packet, sizeof(packet), 2);
        return iResult;
    }

    return -1;
}

int AmultiosNetAdhocTerm()
{
    if (clientConnected)
    {

        MQTTClient_disconnect(clientSocket, 10000);
        MQTTClient_destroy(&clientSocket);
        clientConnected = false;
        NOTICE_LOG(AMULTIOS, "Mqtt client Disconnected");
        return 0;
    }
    return -1;
}

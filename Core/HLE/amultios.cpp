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
#define TIMEOUT 1000L

char ctl_self_topic[28];
char *ptr_self_topic = nullptr;
bool clientConnected = false;
bool ctlRunning = false;
std::thread ctlThread;
MQTTClient clientSocket;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
MQTTClient_willOptions ctl_will = MQTTClient_willOptions_initializer;

volatile MQTTClient_deliveryToken deliveredtoken;

void addAmultiosPeer(AmultiosNetAdhocctlConnectPacketS2C *packet)
{
    if (packet == NULL)
        return;

    // Multithreading Lock
    std::lock_guard<std::recursive_mutex> guard(peerlock);

    SceNetAdhocctlPeerInfo *peer = findFriend(&packet->mac);
    // Already existed
    if (peer != NULL)
    {
        peer->nickname = packet->name;
        peer->mac_addr = packet->mac;
        // Update TimeStamp
        peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
    }
    else
    {
        // Allocate Structure
        peer = (SceNetAdhocctlPeerInfo *)malloc(sizeof(SceNetAdhocctlPeerInfo));
        // Allocated Structure
        if (peer != NULL)
        {
            // Clear Memory
            memset(peer, 0, sizeof(SceNetAdhocctlPeerInfo));

            // Save Nickname
            peer->nickname = packet->name;

            // Save MAC Address
            peer->mac_addr = packet->mac;

            // TimeStamp
            peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();

            // Link to existing Peers
            peer->next = friends;

            // Link into Peerlist
            friends = peer;
        }
    }
}

void deleteAmultiosPeer(SceNetEtherAddr *mac)
{
    // Previous Peer Reference
    SceNetAdhocctlPeerInfo *prev = NULL;

    // Peer Pointer
    SceNetAdhocctlPeerInfo *peer = friends;

    // Iterate Peers
    for (; peer != NULL; peer = peer->next)
    {
        // Found Peer
        if (IsMatch(peer->mac_addr, *mac))
        {
            // Instead of removing it from the list we'll make it timeout since most Matching games are moving group and may still need the peer data
            peer->last_recv = 0;

            // Multithreading Lock
            peerlock.lock();

            // Unlink Left (Beginning)
            if (prev == NULL)
                friends = peer->next;

            // Unlink Left (Other)
            else
                prev->next = peer->next;

            // Multithreading Unlock
            peerlock.unlock();

            // Free Memory
            free(peer);
            peer = NULL;

            // Stop Search
            break;
        }

        // Set Previous Reference
        // TODO: Should this be used by something?
        prev = peer;
    }
}

bool macInNetwork(SceNetEtherAddr *mac)
{
    // Get Local MAC Address
    SceNetEtherAddr localMac;
    getLocalMac(&localMac);
    // Local MAC Requested
    if (memcmp(&localMac, mac, sizeof(SceNetEtherAddr)) == 0)
    {
        return true; // return succes
    }

    // Multithreading Lock
    std::lock_guard<std::recursive_mutex> guard(peerlock);

    // Peer Reference
    SceNetAdhocctlPeerInfo *peer = friends;

    // Iterate Peers
    for (; peer != NULL; peer = peer->next)
    {
        // Found Matching Peer
        if (memcmp(&peer->mac_addr, mac, sizeof(SceNetEtherAddr)) == 0)
        {
            // Return Success
            return true;
        }
    }

    // Peer not found
    return false;
}

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    //INFO_LOG(AMULTIOS, "Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

void connlost(void *context, char *cause)
{
    INFO_LOG(AMULTIOS, "MQTT Connection Lost cause %s", cause);
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

int publish_wait(const char *topic, void *payload, size_t size, int qos, unsigned long timeout)
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
        if (timeout > 0)
        {
            MQTTClient_waitForCompletion(clientSocket, token, timeout);
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
    int rc = 0;
    int topiclen = 28;
    while (ctlRunning && clientConnected)
    {
        char *topicName = ctl_self_topic;
        MQTTClient_message *message = NULL;
        rc = MQTTClient_receive(clientSocket, &topicName, &topiclen, &message, TIMEOUT);
        if (message)
        {
            char *payload_ptr = static_cast<char *>(message->payload);
            if (payload_ptr[0] == OPCODE_CONNECT_BSSID)
            {
                NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] Incoming Opcode connect BSSID");
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
                NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] opcode Connect");
                AmultiosNetAdhocctlConnectPacketS2C *packet = (AmultiosNetAdhocctlConnectPacketS2C *)payload_ptr;
                addAmultiosPeer(packet);
            }
            else if (payload_ptr[0] == OPCODE_DISCONNECT)
            {
                NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] opcode disconnect");
                AmultiosNetAdhocctlDisconnectPacketS2C *packet = (AmultiosNetAdhocctlDisconnectPacketS2C *)payload_ptr;
                deleteAmultiosPeer(&packet->mac);
            }
            else if (payload_ptr[0] == OPCODE_AMULTIOS_LOGOUT)
            {

                NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] Rejected on Network");
                threadStatus = ADHOCCTL_STATE_DISCONNECTED;
            }
            MQTTClient_freeMessage(&message);
            MQTTClient_free(topicName);
        }
    }

    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
    NOTICE_LOG(AMULTIOS, "End of ctl thread");
    return 0;
}

int AmultiosNetAdhocInit()
{

    int rc;
    MQTTClient_create(&clientSocket, ADDRESS, g_Config.sNickName.c_str(), MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    AmultiosNetAdhocctlDisconnectPacketS2C packet;
    packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
    SceNetEtherAddr addres;
    getLocalMac(&addres);
    packet.mac = addres;

    ctl_will.message = (char *)&packet;
    ctl_will.topicName = "SceNetAdhocctl";
    ctl_will.qos = 2;
    ctl_will.retained = 0;
    conn_opts.will = &ctl_will;
    // if ((rc = MQTTClient_setCallbacks(clientSocket, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS)
    // {
    //     ERROR_LOG(AMULTIOS, "Failed to set callback, return code %d\n", rc);
    // };

    if ((rc = MQTTClient_connect(clientSocket, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        ERROR_LOG(AMULTIOS, "Failed to connect, return code %d\n", rc);
        clientConnected = false;
    }
    clientConnected = true;
    snprintf(ctl_self_topic, sizeof(ctl_self_topic), "%02x%02x%02x%02x%02x%02x%s",
             addres.data[0], addres.data[1], addres.data[2], addres.data[3], addres.data[4], addres.data[5], "/SceNetAdhocctl");
    NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] MQTT Subscribe to %s", ctl_self_topic);
    subscribe(ctl_self_topic, 2);
    NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] Mqtt client connected , code %d", rc);
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
                    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
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

int AmultiosNetAdhocctlDisconnect()
{
    if (threadStatus != ADHOCCTL_STATE_DISCONNECTED)
    { // (threadStatus == ADHOCCTL_STATE_CONNECTED)
        // Clear Network Name
        memset(&parameter.group_name, 0, sizeof(parameter.group_name));

        // Set Disconnected State
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;

        AmultiosNetAdhocctlDisconnectPacketS2C packet;

        // Clear Packet Memory
        memset(&packet, 0, sizeof(packet));

        // Set Packet Opcode
        packet.base.opcode = OPCODE_DISCONNECT;

        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;

        // Send Scan Request Packet, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
        int iResult = publish("SceNetAdhocctl", &packet, sizeof(packet), 2);

        // Clear Peer List
        freeFriendsRecursive(friends);
        INFO_LOG(SCENET, "Cleared Peer List.");

        // Delete Peer Reference
        friends = NULL;
    }
}

int AmultiosNetAdhocctlTerm()
{
    if (clientConnected)
    {
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
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

int AmultiosNetAdhocPdpCreate(const char *mac, u32 port, int bufferSize, u32 unknown)
{
    int retval = ERROR_NET_ADHOC_NOT_INITIALIZED;
    SceNetEtherAddr *saddr = (SceNetEtherAddr *)mac;
    if (netAdhocInited)
    {
        // Valid Arguments are supplied
        if (mac != NULL && bufferSize > 0)
        {
            // Valid MAC supplied
            if (isLocalMAC(saddr))
            {


                char udp_topic[26];
                snprintf(udp_topic, sizeof(udp_topic), "PDP/%02x%02x%02x%02x%02x%02x/%d/#",
                         saddr->data[0], saddr->data[1], saddr->data[2], saddr->data[3], saddr->data[4], saddr->data[5], port);

                int rc = subscribe(udp_topic, 0);
                // Valid Socket produced
                if (rc != MQTTCLIENT_FAILURE)
                {
                    // Change socket buffer size when necessary
                    // Allocate Memory for Internal Data

                    SceNetAdhocPdpStat *internal = (SceNetAdhocPdpStat *)malloc(sizeof(SceNetAdhocPdpStat));

                    // Allocated Memory
                    if (internal != NULL)
                    {
                        // Clear Memory
                        memset(internal, 0, sizeof(SceNetAdhocPdpStat));

                        // Find Free Translator Index
                        int i = 0;
                        for (; i < 255; i++)
                        {
                            if (pdp[i] == NULL)
                            {
                                break;
                            }
                            else if (pdp[i] != NULL && pdp[i]->lport == port)
                            {
                                retval = ERROR_NET_ADHOC_PORT_IN_USE;
                            }
                        }

                        // Found Free Translator Index
                        if (i < 255 && retval != ERROR_NET_ADHOC_PORT_IN_USE)
                        {
                            // Fill in Data
                            internal->id = i;
                            internal->laddr = *saddr;
                            internal->lport = port; //should use the port given to the socket (in case it's UNUSED_PORT port) isn't?
                            internal->rcv_sb_cc = bufferSize;

                            // Link Socket to Translator ID
                            pdp[i] = internal;

                            NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] Subscribe to %s", udp_topic);
                            // Success
                            return i + 1;
                        }

                        // Free Memory for Internal Data
                        free(internal);
                        return retval;
                    }
                }
                // Default to No-Space Error
                return ERROR_NET_NO_SPACE;
            }
        }
        return ERROR_NET_ADHOC_INVALID_ARG;
    }

    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag)
{
    SceNetEtherAddr *daddr = (SceNetEtherAddr *)mac;
    uint16_t dport = (uint16_t)port;

    if (netAdhocInited)
    {
        // Valid Port
        if (dport != 0)
        {
            // Valid Data Length
            if (len >= 0)
            { // should we allow 0 size packet (for ping) ?
                // Valid Socket ID
                if (id > 0 && id <= 255 && pdp[id - 1] != NULL)
                {
                    // Cast Socket
                    SceNetAdhocPdpStat *socket = pdp[id - 1];

                    // Valid Data Buffer
                    if (data != NULL)
                    {
                        // Valid Destination Address
                        if (daddr != NULL)
                        {

                            // Single Target
                            if (!isBroadcastMAC(daddr))
                            {
                                // Fill in Target Structure

                                //const char test = "PDP/aabbccddeeff/65536/aabbccddeeff/65535";

                                // Get Peer IP
                                if (macInNetwork((SceNetEtherAddr *)daddr))
                                {
                                    // Acquire Network Lock
                                    //_acquireNetworkLock();
                                    int rc;
                                    char pdp_single_topic[50];

                                    snprintf(pdp_single_topic, sizeof(pdp_single_topic), "PDP/%02x%02x%02x%02x%02x%02x/%d/%02x%02x%02x%02x%02x%02x/%d",
                                             daddr->data[0], daddr->data[1], daddr->data[2], daddr->data[3], daddr->data[4], daddr->data[5], dport, socket->laddr.data[0], socket->laddr.data[1], socket->laddr.data[2], socket->laddr.data[3], socket->laddr.data[4], socket->laddr.data[5], socket->lport);

                                    NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] PDP send topic single %s", pdp_single_topic);

                                    if (flag)
                                    {
                                        rc = publish(pdp_single_topic, data, len, 0);

                                        if (rc == MQTTCLIENT_SUCCESS)
                                        {
                                            return 0;
                                        }
                                        return ERROR_NET_ADHOC_WOULD_BLOCK;
                                    }

                                    rc = publish_wait(pdp_single_topic, data, len, 1, timeout);

                                    if (rc == MQTTCLIENT_SUCCESS)
                                    {
                                        return 0;
                                    }
                                    return ERROR_NET_ADHOC_TIMEOUT;
                                }
                            }

                            // Broadcast Target
                            else
                            {

                                // Acquire Peer Lock
                                peerlock.lock();

                                // Iterate Peers
                                SceNetAdhocctlPeerInfo *peer = friends;
                                for (; peer != NULL; peer = peer->next)
                                {
                                    int rc;
                                    char pdp_single_topic[50];
                                    
                                    snprintf(pdp_single_topic, sizeof(pdp_single_topic), "PDP/%02x%02x%02x%02x%02x%02x/%d/%02x%02x%02x%02x%02x%02x/%d",
                                             peer->mac_addr.data[0], peer->mac_addr.data[1], peer->mac_addr.data[2], peer->mac_addr.data[3], peer->mac_addr.data[4], peer->mac_addr.data[5], dport, socket->laddr.data[0], socket->laddr.data[1], socket->laddr.data[2], socket->laddr.data[3], socket->laddr.data[4], socket->laddr.data[5], socket->lport);

                                    if (flag)
                                    {
                                        rc = publish(pdp_single_topic, data, len, 0);
                                    }else{
                                        rc = publish_wait(pdp_single_topic, data, len, 1,timeout);
                                    }
                                    
                                    if(rc == MQTTCLIENT_SUCCESS){
                                    NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] PDP send topic broadcast %s", pdp_single_topic);
                                    }
                                }

                                // Free Peer Lock
                                peerlock.unlock();

                                // Free Network Lock
                                //_freeNetworkLock();

                                // Success, Broadcast never fails!
                                return 0;
                            }
                        }

                        // Invalid Destination Address
                        return ERROR_NET_ADHOC_INVALID_ADDR;
                    }

                    // Invalid Argument
                    return ERROR_NET_ADHOC_INVALID_ARG;
                }

                // Invalid Socket ID
                return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
            }

            // Invalid Data Length
            return ERROR_NET_ADHOC_INVALID_DATALEN;
        }

        // Invalid Destination Port
        return ERROR_NET_ADHOC_INVALID_PORT;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

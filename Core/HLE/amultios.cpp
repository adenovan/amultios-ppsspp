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
#include "util/text/parsers.h"

#define ADDRESS "tcp://amultios.net:1883"

std::thread ctlThread;
AmultiosMqtt *ctl_mqtt = nullptr;

std::thread pdpThread;
AmultiosMqtt *pdp_mqtt = nullptr;
std::mutex pdp_queue_mutex;
std::vector<std::string> pdp_topic;
std::vector<PDPMessage> pdp_queue;

std::thread ptpThread;
AmultiosMqtt *ptp_mqtt = nullptr;
std::mutex ptp_queue_mutex;
std::vector<PDPMessage> ptp_queue;

bool ctlRunning = false;
bool pdpRunning = false;
bool ptpRunning = false;

volatile MQTTAsync_token token;

void getMac(SceNetEtherAddr *addr, std::string const &s)
{
    // Read MAC Address from config
    uint8_t mac[ETHER_ADDR_LEN] = {0};
    if (!ParseMacAddress(s.c_str(), mac))
    {
        ERROR_LOG(SCENET, "Error parsing mac address %s", s.c_str());
    }
    memcpy(addr, mac, ETHER_ADDR_LEN);
}

std::vector<std::string> explode(std::string const &s, char delim)
{
    std::vector<std::string> result;
    std::istringstream iss(s);

    for (std::string token; std::getline(iss, token, delim);)
    {
        result.push_back(std::move(token));
    }

    return result;
}

std::string getMacString(SceNetEtherAddr *addr)
{
    char macAddr[18];
    snprintf(macAddr, sizeof(macAddr), "%02x:%02x:%02x:%02x:%02x:%02x", addr->data[0], addr->data[1], addr->data[2], addr->data[3], addr->data[4], addr->data[5]);
    return std::string(macAddr);
}

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

int publish(AmultiosMqtt * amultios_mqtt, const char *topic, void *payload, size_t size, int qos, unsigned long timeout)
{
    int rc = MQTTASYNC_FAILURE;
    if (amultios_mqtt != nullptr && amultios_mqtt->connected)
    {
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        MQTTAsync_message msg = MQTTAsync_message_initializer;
        msg.payload = payload;
        msg.payloadlen = (int)size;
        msg.qos = qos;
        msg.retained = 0;

        amultios_mqtt->pub_topic_latest = topic;
        amultios_mqtt->pub_payload_len_latest = size;
        amultios_mqtt->qos_latest = qos;

        opts.context = amultios_mqtt;
        opts.onSuccess = publish_success;
        opts.onFailure = publish_failure;
        rc = MQTTAsync_sendMessage(amultios_mqtt->client, topic, &msg, &opts);
        if (timeout > 0)
        {
            rc = MQTTAsync_waitForCompletion(amultios_mqtt->client, token, timeout);
        }
    }
    return rc;
}

int subscribe(AmultiosMqtt * amultios_mqtt, const char *topic, int qos)
{
    if (amultios_mqtt != nullptr && amultios_mqtt->connected)
    {
        NOTICE_LOG(AMULTIOS, "Amultios_mqtt subscribe to topic:[%s] qos:[%d]", topic, qos);
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        amultios_mqtt->sub_topic_latest = topic;
        amultios_mqtt->qos_latest = qos;
        opts.context = amultios_mqtt;
        opts.onSuccess = subscribe_success;
        opts.onFailure = subscribe_failure;
        return MQTTAsync_subscribe(amultios_mqtt->client, topic, qos, &opts);
    }
    return MQTTASYNC_FAILURE;
}

int unsubscribe(AmultiosMqtt *amultios_mqtt, const char *topic)
{
    if (amultios_mqtt != nullptr && amultios_mqtt->connected)
    {
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.context = amultios_mqtt;
        amultios_mqtt->sub_topic_latest = topic;
        opts.onSuccess = unsubscribe_success;
        opts.onFailure = unsubscribe_failure;
        return MQTTAsync_unsubscribe(amultios_mqtt->client, topic, &opts);
    }
    return MQTTASYNC_FAILURE;
}

void ctl_connect_success(void *context, MQTTAsync_successData *response)
{
    NOTICE_LOG(AMULTIOS, "CTL_MQTT CONNECTION Success");
    ctl_mqtt->connected = true;
};

void ctl_connect_failure(void *context, MQTTAsync_failureData *response)
{
    NOTICE_LOG(AMULTIOS, "CTL_MQTT CONNECTION DISCONNECTED");
    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
    ctl_mqtt->connected = false;
};

void ctl_disconnect_success(void *context, MQTTAsync_successData *response)
{
    NOTICE_LOG(AMULTIOS, "CTL_MQTT CONNECTION DISCONNECTED");
    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
    ctl_mqtt->connected = false;
};

void ctl_disconnect_failure(void *context, MQTTAsync_failureData *response)
{
    NOTICE_LOG(AMULTIOS, "CTL_MQTT CONNECTION DISCONNECTED");
    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
    ctl_mqtt->connected = false;
};

void ctl_connect_lost(void * context, char *cause)
{
    NOTICE_LOG(AMULTIOS, "CTL_MQTT CONNECTION LOST %s", cause);
    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
    ctl_mqtt->connected = false;
};

int ctl_message_arrived(void * context, char * topicName, int topicLen, MQTTAsync_message *message)
{
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
        MQTTAsync_freeMessage(&message);
        MQTTAsync_free(topicName);
    }
    return 1;
};

void pdp_connect_success(void *context, MQTTAsync_successData *response);
void pdp_connect_failure(void *context, char *cause);
void pdp_disconnect(void *context, MQTTAsync_successData *response);
void pdp_connect_lost(void *context, char *cause);
int pdp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);

void ptp_connect_success(void * context, MQTTAsync_successData *response);
void ptp_connect_failure(void * context, char *cause);
void ptp_disconnect(void * context, MQTTAsync_successData *response);
void ptp_connect_lost(void * context, char *cause);
int ptp_message_arrived(void * context, char *topicName, int topicLen, MQTTAsync_message *message);

void publish_success(void * context, MQTTAsync_successData *response)
{

    AmultiosMqtt *ptr = (AmultiosMqtt *)context;
    NOTICE_LOG(AMULTIOS, "Publish Success %s", response->alt.pub.destinationName);
};

void publish_failure(void *context, MQTTAsync_failureData *response)
{
    ERROR_LOG(AMULTIOS, "Publish Failure %d", response->code);
};

void subscribe_success(void *context, MQTTAsync_successData *response)
{
    NOTICE_LOG(AMULTIOS, "Subscribe Success %s", response->alt.pub.destinationName);
};

void subscribe_failure(void *context, MQTTAsync_failureData *response)
{
    ERROR_LOG(AMULTIOS, "Subscribe Failure %d", response->code);
};

void unsubscribe_success(void *context, MQTTAsync_successData *response)
{
    NOTICE_LOG(AMULTIOS, "Unsubscribe Success %s", response->alt.pub.destinationName);
};

void unsubscribe_failure(void *context, MQTTAsync_failureData *response)
{
    ERROR_LOG(AMULTIOS, "Unsubscribe Failure %d", response->code);
};

int __AMULTIOS_CTL_INIT()
{
    int rc = MQTTASYNC_FAILURE;
    if (ctl_mqtt == nullptr)
    {
        ctl_mqtt = new AmultiosMqtt();
        ctl_mqtt->subscribed = false;
        ctl_mqtt->timeout = 60000L;
        MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
        MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;

        ctl_mqtt->mqtt_id = "CTL/" + g_Config.sNickName;
        rc = MQTTAsync_create(&ctl_mqtt->client, ADDRESS, ctl_mqtt->mqtt_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
        MQTTAsync_setCallbacks(ctl_mqtt->client, ctl_mqtt, ctl_connect_lost, ctl_message_arrived, NULL);

        opts.context = ctl_mqtt;
        opts.keepAliveInterval = 10;
        opts.retryInterval = 0;
        opts.cleansession = 1;
        opts.connectTimeout = 20;
        opts.onSuccess = ctl_connect_success;
        opts.onFailure = ctl_connect_failure;

        // initialize will message
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;

        will.message = (char *)&packet;
        will.topicName = "SceNetAdhocctl";
        will.qos = 2;
        will.retained = 0;
        opts.will = &will;

        if ((rc = MQTTAsync_connect(ctl_mqtt->client, &opts)) != MQTTASYNC_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "Failed to connect, return code %d\n", rc);
            return rc;
        }
        ctl_mqtt->connected = true;
        ctl_mqtt->port = 27312;
        ctl_mqtt->sub_topic = g_Config.sMACAddress + "/SceNetAdhocctl";
        ctl_mqtt->pub_topic = "SceNetAdhocctl";

        while (!ctl_mqtt->subscribed)
        {
            sleep_ms(1);
        }

        while (ctlRunning)
            ;
    }

    NOTICE_LOG(AMULTIOS, "CTL_MQTT FINISHED");
    return rc;
}

int __AMULTIOS_CTL_SHUTDOWN()
{
    int rc = MQTTASYNC_SUCCESS;
    if (ctl_mqtt != nullptr)
    {
        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        opts.context = ctl_mqtt;
        opts.timeout = 1;
        opts.onSuccess = ctl_disconnect_success;
        opts.onFailure = ctl_disconnect_failure;
        int rc = MQTTAsync_disconnect(ctl_mqtt->client, &opts);
        MQTTAsync_destroy(&ctl_mqtt->client);
        ctl_mqtt->connected = false;
        delete ctl_mqtt;
        ctl_mqtt = nullptr;
        NOTICE_LOG(AMULTIOS, "ctl_mqtt client disconnected %d", rc);
    }
    return rc;
}

int AmultiosNetAdhocInit()
{
    int rc = MQTTASYNC_FAILURE;
    if (ctl_mqtt == nullptr)
    {
        return rc;
    }
    rc = subscribe(ctl_mqtt, ctl_mqtt->sub_topic.c_str(), 2);
    NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] MQTT Subscribe to %s", ctl_mqtt->sub_topic.c_str());
    ctl_mqtt->subscribed = rc == MQTTASYNC_SUCCESS;
    return rc;
}

int AmultiosNetAdhocctlInit(SceNetAdhocctlAdhocId *adhoc_id)
{
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        SceNetAdhocctlLoginPacketC2S packet;
        packet.base.opcode = OPCODE_LOGIN;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        strcpy((char *)packet.name.data, g_Config.sNickName.c_str());
        memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);
        return publish(ctl_mqtt, ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
    }
    return MQTTASYNC_FAILURE;
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

                int iResult;

                if (ctl_mqtt != nullptr && ctl_mqtt->connected)
                {
                    iResult = publish(ctl_mqtt, ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
                }

                if (iResult != MQTTASYNC_SUCCESS)
                {
                    ERROR_LOG(AMULTIOS, "Mqtt Error when sending reason %d", iResult);
                    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
                }

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
    if (netAdhocctlInited)
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

            int iResult = MQTTASYNC_FAILURE;

            if (ctl_mqtt != nullptr && ctl_mqtt->connected)
            {
                iResult = publish(ctl_mqtt, ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
            }

            if (iResult != MQTTASYNC_SUCCESS)
            {
                ERROR_LOG(AMULTIOS, "ctl_mqtt Error when sending scan reason %d", iResult);
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

        int iResult;
        if (ctl_mqtt != nullptr && ctl_mqtt->connected)
        {
            iResult = publish(ctl_mqtt, ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
        }
        // Clear Peer List
        freeFriendsRecursive(friends);
        INFO_LOG(AMULTIOS, "Cleared Peer List.");

        // Delete Peer Reference
        friends = NULL;
    }
    return 0;
}

int AmultiosNetAdhocctlTerm()
{
    int rc = MQTTASYNC_FAILURE;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        rc = publish(ctl_mqtt, ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
        return rc;
    }

    return rc;
}

int AmultiosNetAdhocTerm()
{
    int rc;

    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        rc = unsubscribe(ctl_mqtt, ctl_mqtt->sub_topic.c_str());
        return rc;
    }

    return rc;
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

                int rc;
                std::string sub_topic = "PDP/" + getMacString(saddr) + "/" + std::to_string(port) + "/#";

                // Valid Socket produced
                if ((rc = subscribe(pdp_mqtt, sub_topic.c_str(), 0) == MQTTASYNC_SUCCESS))
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
                            if (pdp[i] != NULL && pdp[i]->lport == port)
                            {
                                retval = ERROR_NET_ADHOC_PORT_IN_USE;
                            }
                            else if (pdp[i] == NULL)
                            {
                                break;
                            }
                        }

                        // Found Free Translator Index
                        if (i < 255 && retval != ERROR_NET_ADHOC_PORT_IN_USE)
                        {
                            // Fill in Data
                            internal->id = rc;
                            internal->laddr = *saddr;
                            internal->lport = port; //should use the port given to the socket (in case it's UNUSED_PORT port) isn't?
                            internal->rcv_sb_cc = bufferSize;

                            // Link Socket to Translator ID
                            pdp[i] = internal;
                            pdp_topic.at(i) = sub_topic;

                            // Success
                            return i + 1;
                        }

                        // Free Memory for Internal Data
                        free(internal);
                        return retval;
                    }
                    free(internal);
                }

                ERROR_LOG(AMULTIOS, "PDP_MQTT CREATE FAILED %d , topic %s ", rc, sub_topic.c_str());
                return ERROR_NET_NO_SPACE;
            }
        }
        return ERROR_NET_ADHOC_INVALID_ARG;
    }

    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag)
{
    //INFO_LOG(AMULTIOS, "AmultiosNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i)", id, mac, port, data, len, timeout, flag);
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
                                    SceNetEtherAddr *saddr = (SceNetEtherAddr *)socket->laddr.data;
                                    std::string pdp_single_topic = "PDP/" + getMacString(daddr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport);
                                    NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] PDP send topic single %s", pdp_single_topic.c_str());

                                    if (flag)
                                    {
                                        rc = publish(pdp_mqtt, pdp_single_topic.c_str(), data, len, 0, 0);

                                        if (rc == MQTTASYNC_SUCCESS)
                                        {
                                            return 0;
                                        }
                                        return ERROR_NET_ADHOC_WOULD_BLOCK;
                                    }

                                    rc = publish(pdp_mqtt, pdp_single_topic.c_str(), data, len, 1, timeout);

                                    if (rc == MQTTASYNC_SUCCESS)
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
                                SceNetEtherAddr *saddr = (SceNetEtherAddr *)socket->laddr.data;
                                for (; peer != NULL; peer = peer->next)
                                {
                                    int rc;
                                    std::string pdp_single_topic = "PDP/" + getMacString(daddr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport);

                                    if (flag)
                                    {
                                        rc = publish(pdp_mqtt, pdp_single_topic.c_str(), data, len, 0, 0);
                                    }
                                    else
                                    {
                                        rc = publish(pdp_mqtt, pdp_single_topic.c_str(), data, len, 1, timeout);
                                    }

                                    if (rc == MQTTASYNC_SUCCESS)
                                    {
                                        NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] PDP send topic broadcast %s", pdp_single_topic.c_str());
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

int AmultiosNetAdhocPdpRecv(int id, void *addr, void *port, void *buf, void *dataLength, u32 timeout, int flag)
{
    SceNetEtherAddr *saddr = (SceNetEtherAddr *)addr;
    uint16_t *sport = (uint16_t *)port; //Looking at Quake3 sourcecode (net_adhoc.c) this is an "int" (32bit) but changing here to 32bit will cause FF-Type0 to see duplicated Host (thinking it was from a different host)
    int *len = (int *)dataLength;
    if (netAdhocInited)
    {
        // Valid Socket ID
        if (id > 0 && id <= 255 && pdp[id - 1] != NULL)
        {
            // Cast Socket
            SceNetAdhocPdpStat *socket = pdp[id - 1];

            // Valid Arguments
            if (saddr != NULL && port != NULL && buf != NULL && len != NULL && *len > 0)
            {
                if (flag)
                    timeout = 0;

                if (pdp_queue.size() > 0)
                {

                    int i = 0;
                    PDPMessage packet = pdp_queue.at(i);
                    memcpy(buf, packet.message->payload, packet.message->payloadlen);
                    *saddr = packet.sourceMac;
                    *sport = packet.port;
                    //Save Length
                    *len = packet.message->payloadlen;
                    return 0;
                }

                if (flag)
                    return ERROR_NET_ADHOC_WOULD_BLOCK;
                return ERROR_NET_ADHOC_TIMEOUT;
            }

            // Invalid Argument
            return ERROR_NET_ADHOC_INVALID_ARG;
        }

        // Invalid Socket ID
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPdpDelete(int id, int unknown)
{
    // WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?

    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Arguments
        if (id > 0 && id <= 255)
        {
            // Cast Socket
            SceNetAdhocPdpStat *sock = pdp[id - 1];

            // Valid Socket
            if (sock != NULL)
            {
                // Close Connection
                //closesocket(sock->id);
                // Remove Port Forward from Router
                //sceNetPortClose("UDP", sock->lport);

                // Free Memory
                // free(sock);

                // Free Translation Slot
                pdp[id - 1] = NULL;
                int rc = unsubscribe(pdp_mqtt, pdp_topic.at(id - 1).c_str());
                ctl_mqtt->subscribed = false;
                // Success
                return 0;
            }

            // Invalid Socket ID
            return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
        }

        // Invalid Argument
        return ERROR_NET_ADHOC_INVALID_ARG;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

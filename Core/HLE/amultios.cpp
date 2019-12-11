#include "Core/Core.h"
#include "Core/Host.h"
#include "i18n/i18n.h"
#include "base/timeutil.h"
#include "amultios.h"
#include "util/text/parsers.h"

#define ADDRESS "tcp://amultios.net:1883"

bool ctlInited = false;
bool ctlRunning = false;
std::thread ctlThread;
std::shared_ptr<AmultiosMqtt> g_ctl_mqtt = nullptr;
std::mutex ctl_mqtt_mutex, ctl_running_mutex;
std::condition_variable ctl_running_cv;

bool pdpInited = false;
bool pdpRunning = false;
std::thread pdpThread;
std::shared_ptr<AmultiosMqtt> g_pdp_mqtt = nullptr;
std::mutex pdp_mqtt_mutex, pdp_running_mutex;
std::condition_variable pdp_running_cv;

std::vector<std::string> pdp_topic(255);
std::mutex pdp_queue_mutex;
std::vector<PDPMessage> pdp_queue;

bool ptpInited = false;
bool ptpRunning = false;
std::thread ptpThread;

std::shared_ptr<AmultiosMqtt> g_ptp_mqtt = nullptr;
std::mutex ptp_mqtt_mutex, ptp_running_mutex;
std::condition_variable ptp_running_cv;
std::mutex ptp_queue_mutex;
std::vector<PTPMessage> ptp_queue;

std::vector<std::string> ptp_sub_topic(255);
std::vector<std::string> ptp_pub_topic(255);
std::vector<std::string> ptp_relay_topic(255);
std::mutex ptp_peer_mutex;
std::vector<PTPConnection> ptp_peer_connection;

volatile MQTTAsync_token token;

bool isSameMAC(const SceNetEtherAddr *addr, const SceNetEtherAddr *addr2)
{
    // Compare MAC Addresses
    int match = memcmp((const void *)addr, (const void *)addr2, ETHER_ADDR_LEN);

    // Return Result
    return (match == 0);
}

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
    std::string::const_iterator start = s.begin();
    std::string::const_iterator end = s.end();
    std::string::const_iterator next = std::find(start, end, delim);
    while (next != end)
    {
        result.push_back(std::string(start, next));
        start = next + 1;
        next = std::find(start, end, delim);
    }
    result.push_back(std::string(start, next));
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
        NOTICE_LOG(AMULTIOS, "Adding Existing Peer [%s]", getMacString(&packet->mac).c_str());
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
            NOTICE_LOG(AMULTIOS, "Adding Peer [%s]", getMacString(&packet->mac).c_str());
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
            NOTICE_LOG(AMULTIOS, "Removing Peer [%s]", getMacString(mac).c_str());
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

//start of ctl relay

int ctl_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout)
{
    int rc = MQTTASYNC_FAILURE;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        MQTTAsync_message msg = MQTTAsync_message_initializer;
        msg.payload = payload;
        msg.payloadlen = (int)size;
        msg.qos = qos;
        msg.retained = 0;

        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->pub_topic_latest = topic;
            ctl_mqtt->pub_payload_len_latest = size;
            ctl_mqtt->qos_latest = qos;
        }
        opts.onSuccess = ctl_publish_success;
        opts.onFailure = ctl_publish_failure;
        rc = MQTTAsync_sendMessage(ctl_mqtt->client, topic, &msg, &opts);
        if (timeout > 0)
        {
            rc = MQTTAsync_waitForCompletion(ctl_mqtt->client, rc, timeout);
        }
    }
    return rc;
}

int ctl_subscribe(const char *topic, int qos)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        //NOTICE_LOG(AMULTIOS, "Amultios_mqtt subscribe to topic:[%s] qos:[%d]", topic, qos);
        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->sub_topic_latest = topic;
            ctl_mqtt->qos_latest = qos;
        }

        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.onSuccess = ctl_subscribe_success;
        opts.onFailure = ctl_subscribe_failure;
        return MQTTAsync_subscribe(ctl_mqtt->client, topic, qos, &opts);
    }
    return MQTTASYNC_FAILURE;
}

int ctl_unsubscribe(const char *topic)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->sub_topic_latest = topic;
        }

        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.onSuccess = ctl_unsubscribe_success;
        opts.onFailure = ctl_unsubscribe_failure;
        return MQTTAsync_unsubscribe(ctl_mqtt->client, topic, &opts);
    }
    return MQTTASYNC_FAILURE;
}

void ctl_publish_success(void *context, MQTTAsync_successData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        VERBOSE_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%lu] qos [%d] ", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->pub_topic_latest.c_str(), ctl_mqtt->pub_payload_len_latest, ctl_mqtt->qos_latest);
    }
};

void ctl_publish_failure(void *context, MQTTAsync_failureData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Publish failure [%d] on topic [%s] payload_len [%lu] qos [%d] ", ctl_mqtt->mqtt_id.c_str(), response->code, ctl_mqtt->pub_topic_latest.c_str(), ctl_mqtt->pub_payload_len_latest, ctl_mqtt->qos_latest);
    }
};

void ctl_subscribe_success(void *context, MQTTAsync_successData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->sub_topic_latest.c_str(), ctl_mqtt->qos_latest);
        std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
        ctl_mqtt->subscribed += 1;
    }
};

void ctl_subscribe_failure(void *context, MQTTAsync_failureData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Subscribe Failure on topic [%s] qos [%d]", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->sub_topic_latest.c_str(), ctl_mqtt->qos_latest);
    }
};

void ctl_unsubscribe_success(void *context, MQTTAsync_successData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->sub_topic_latest.c_str());
        std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
        ctl_mqtt->subscribed -= 1;
    }
};

void ctl_unsubscribe_failure(void *context, MQTTAsync_failureData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Unsubscribe Failure [%d] on topic [%s]", ctl_mqtt->mqtt_id.c_str(), response->code, ctl_mqtt->sub_topic_latest.c_str());
    }
};

void ctl_connect_success(void *context, MQTTAsync_successData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Connect Success", ctl_mqtt->mqtt_id.c_str());
        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->connected = true;
            ctl_mqtt->reconnectInProgress = false;
        }
    }
};

void ctl_connect_failure(void *context, MQTTAsync_failureData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        //ERROR_LOG(AMULTIOS, "[%s] Connect Failure", ptr->mqtt_id.c_str());
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;

        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->connected = false;
            ctl_mqtt->reconnectInProgress = false;
        }
    }
};

void ctl_disconnect_success(void *context, MQTTAsync_successData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Disconnect Success", ctl_mqtt->mqtt_id.c_str());
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;
        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->connected = false;
        }
    }
};

void ctl_disconnect_failure(void *context, MQTTAsync_failureData *response)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Disconnect Failure", ctl_mqtt->mqtt_id.c_str());
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;
        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->connected = false;
        }
    }
};

void ctl_connect_lost(void *context, char *cause)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;
        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->connected = false;
            ctl_mqtt->reconnectInProgress = false;
        }
    }
};

int ctl_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
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

//start of pdp relay

int pdp_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout)
{
    int rc = MQTTASYNC_FAILURE;
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr && pdp_mqtt->connected)
    {
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        MQTTAsync_message msg = MQTTAsync_message_initializer;
        msg.payload = payload;
        msg.payloadlen = (int)size;
        msg.qos = qos;
        msg.retained = 0;
        {
            std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            pdp_mqtt->pub_topic_latest = topic;
            pdp_mqtt->pub_payload_len_latest = size;
            pdp_mqtt->qos_latest = qos;
        }
        opts.onSuccess = pdp_publish_success;
        opts.onFailure = pdp_publish_failure;
        rc = MQTTAsync_sendMessage(pdp_mqtt->client, topic, &msg, &opts);
        if (timeout > 0)
        {
            rc = MQTTAsync_waitForCompletion(pdp_mqtt->client, rc, timeout);
        }
    }
    return rc;
}

int pdp_subscribe(const char *topic, int qos)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr && pdp_mqtt->connected)
    {
        //NOTICE_LOG(AMULTIOS, "Amultios_mqtt subscribe to topic:[%s] qos:[%d]", topic, qos);
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

        {
            std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            pdp_mqtt->sub_topic_latest = topic;
            pdp_mqtt->qos_latest = qos;
        }

        opts.onSuccess = pdp_subscribe_success;
        opts.onFailure = pdp_subscribe_failure;
        return MQTTAsync_subscribe(pdp_mqtt->client, topic, qos, &opts);
    }
    return MQTTASYNC_FAILURE;
}

int pdp_unsubscribe(const char *topic)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr && pdp_mqtt->connected)
    {
        {
            std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            pdp_mqtt->sub_topic_latest = topic;
        }

        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.onSuccess = pdp_unsubscribe_success;
        opts.onFailure = pdp_unsubscribe_failure;
        return MQTTAsync_unsubscribe(pdp_mqtt->client, topic, &opts);
    }
    return MQTTASYNC_FAILURE;
}

void pdp_publish_success(void *context, MQTTAsync_successData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        VERBOSE_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%lu] qos [%d] ", pdp_mqtt->mqtt_id.c_str(), pdp_mqtt->pub_topic_latest.c_str(), pdp_mqtt->pub_payload_len_latest, pdp_mqtt->qos_latest);
    }
};

void pdp_publish_failure(void *context, MQTTAsync_failureData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Publish failure [%d] on topic [%s] payload_len [%lu] qos [%d] ", pdp_mqtt->mqtt_id.c_str(), response->code, pdp_mqtt->pub_topic_latest.c_str(), pdp_mqtt->pub_payload_len_latest, pdp_mqtt->qos_latest);
    }
};

void pdp_subscribe_success(void *context, MQTTAsync_successData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", pdp_mqtt->mqtt_id.c_str(), pdp_mqtt->sub_topic_latest.c_str(), pdp_mqtt->qos_latest);
        std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        pdp_mqtt->subscribed += 1;
    }
};

void pdp_subscribe_failure(void *context, MQTTAsync_failureData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Subscribe Failure on topic [%s] qos [%d]", pdp_mqtt->mqtt_id.c_str(), pdp_mqtt->sub_topic_latest.c_str(), pdp_mqtt->qos_latest);
    }
};

void pdp_unsubscribe_success(void *context, MQTTAsync_successData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", pdp_mqtt->mqtt_id.c_str(), pdp_mqtt->sub_topic_latest.c_str());
        std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        pdp_mqtt->subscribed -= 1;
    }
};

void pdp_unsubscribe_failure(void *context, MQTTAsync_failureData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Unsubscribe Failure [%d] on topic [%s]", pdp_mqtt->mqtt_id.c_str(), response->code, pdp_mqtt->sub_topic_latest.c_str());
    }
};

void pdp_connect_success(void *context, MQTTAsync_successData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Connect Success", pdp_mqtt->mqtt_id.c_str());
        std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        pdp_mqtt->connected = true;
        pdp_mqtt->reconnectInProgress = false;
    }
};

void pdp_connect_failure(void *context, MQTTAsync_failureData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Connect Failure", pdp_mqtt->mqtt_id.c_str());
        {
            std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            pdp_mqtt->connected = false;
            pdp_mqtt->reconnectInProgress = false;
        }
    }
};

void pdp_disconnect_success(void *context, MQTTAsync_successData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Disconnect Success", pdp_mqtt->mqtt_id.c_str());
        std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        pdp_mqtt->connected = false;
    }
};

void pdp_disconnect_failure(void *context, MQTTAsync_failureData *response)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Disconnect Failure", pdp_mqtt->mqtt_id.c_str());
        std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        pdp_mqtt->connected = false;
    }
};

void pdp_connect_lost(void *context, char *cause)
{
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {
        WARN_LOG(AMULTIOS, "[%s] Connection Lost cause [%s]", pdp_mqtt->mqtt_id.c_str(), cause);
        {
            std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            pdp_mqtt->connected = false;
        }
    }
};

int pdp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    if (message)
    {
        //AmultiosMqtt *ptr = (AmultiosMqtt *)context;

        std::string topic = topicName;
        std::vector<std::string> topic_explode = explode(topic, '/');
        PDPMessage msg;
        msg.sport = std::stoi(topic_explode.at(4));
        getMac(&msg.sourceMac, topic_explode.at(3));
        msg.dport = std::stoi(topic_explode.at(2));
        getMac(&msg.destinationMac, topic_explode.at(1));
        //NOTICE_LOG(AMULTIOS, "[%s] got pdp message src [%s]:[%s] dst [%s]:[%s] messagelen[%d] topic [%s] topiclen [%d] ", ptr->mqtt_id.c_str(), topic_explode.at(3).c_str(), topic_explode.at(4).c_str(), topic_explode.at(1).c_str(), topic_explode.at(2).c_str(), message->payloadlen, topicName, topicLen);
        msg.message = message;
        msg.topicName = topicName,
        msg.topicLen = topicLen;

        std::lock_guard<std::mutex> lock(pdp_queue_mutex);
        pdp_queue.push_back(msg);
        //MQTTAsync_freeMessage(&message);
        //MQTTAsync_free(topicName);
    }
    return 1;
};

// start of ptp relay
int ptp_publish(const char *topic, void *payload, size_t size, int qos, unsigned long timeout)
{
    int rc = MQTTASYNC_FAILURE;
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr && ptp_mqtt->connected)
    {
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        MQTTAsync_message msg = MQTTAsync_message_initializer;
        msg.payload = payload;
        msg.payloadlen = (int)size;
        msg.qos = qos;
        msg.retained = 0;

        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->pub_topic_latest = topic;
            ptp_mqtt->pub_payload_len_latest = size;
            ptp_mqtt->qos_latest = qos;
        }

        opts.onSuccess = ptp_publish_success;
        opts.onFailure = ptp_publish_failure;
        rc = MQTTAsync_sendMessage(ptp_mqtt->client, topic, &msg, &opts);
        if (timeout > 0)
        {
            rc = MQTTAsync_waitForCompletion(ptp_mqtt->client, rc, timeout);
        }
        NOTICE_LOG(AMULTIOS, "[%s] publish on topic [%s] payload_len [%lu] qos [%i] timeout [%lu]", ptp_mqtt->mqtt_id.c_str(), topic, size, qos, timeout);
    }
    return rc;
}

int ptp_subscribe(const char *topic, int qos)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr && ptp_mqtt->connected)
    {
        INFO_LOG(AMULTIOS, "ptp_mqtt subscribe to topic:[%s] qos:[%d]", topic, qos);
        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->sub_topic_latest = topic;
            ptp_mqtt->qos_latest = qos;
        }
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.onSuccess = ptp_subscribe_success;
        opts.onFailure = ptp_subscribe_failure;
        return MQTTAsync_subscribe(ptp_mqtt->client, topic, qos, &opts);
    }
    return MQTTASYNC_FAILURE;
}

int ptp_unsubscribe(const char *topic)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr && ptp_mqtt->connected)
    {
        INFO_LOG(AMULTIOS, "ptp_mqtt unsubcribe to topic:[%s]", topic);
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->sub_topic_latest = topic;
        }
        opts.onSuccess = ptp_unsubscribe_success;
        opts.onFailure = ptp_unsubscribe_failure;
        return MQTTAsync_unsubscribe(ptp_mqtt->client, topic, &opts);
    }
    return MQTTASYNC_FAILURE;
}

void ptp_publish_success(void *context, MQTTAsync_successData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        //INFO_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%lu] qos [%d] ", ptp_mqtt->mqtt_id.c_str(), ptp_mqtt->pub_topic_latest.c_str(), ptp_mqtt->pub_payload_len_latest, ptp_mqtt->qos_latest);
    }
};

void ptp_publish_failure(void *context, MQTTAsync_failureData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        //ERROR_LOG(AMULTIOS, "[%s] Publish failure [%d] on topic [%s] payload_len [%lu] qos [%d] ", ptp_mqtt->mqtt_id.c_str(), response->code, ptp_mqtt->pub_topic_latest.c_str(), ptp_mqtt->pub_payload_len_latest, ptp_mqtt->qos_latest);
    }
};

void ptp_subscribe_success(void *context, MQTTAsync_successData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        //INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", ptp_mqtt->mqtt_id.c_str(), ptp_mqtt->sub_topic_latest.c_str(), ptp_mqtt->qos_latest);
        std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
        ptp_mqtt->subscribed += 1;
    }
};

void ptp_subscribe_failure(void *context, MQTTAsync_failureData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        //ERROR_LOG(AMULTIOS, "[%s] Subscribe Failure on topic [%s] qos [%d]", ptp_mqtt->mqtt_id.c_str(), ptp_mqtt->sub_topic_latest.c_str(), ptp_mqtt->qos_latest);
    }
};

void ptp_unsubscribe_success(void *context, MQTTAsync_successData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        //INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", ptp_mqtt->mqtt_id.c_str(), ptp_mqtt->sub_topic_latest.c_str());
        std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
        ptp_mqtt->subscribed -= 1;
    }
};

void ptp_unsubscribe_failure(void *context, MQTTAsync_failureData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        //ERROR_LOG(AMULTIOS, "[%s] Unsubscribe Failure [%d] on topic [%s]", ptp_mqtt->mqtt_id.c_str(), response->code, ptp_mqtt->sub_topic_latest.c_str());
    }
};

void ptp_connect_success(void *context, MQTTAsync_successData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Connect Success", ptp_mqtt->mqtt_id.c_str());
        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->connected = true;
            ptp_mqtt->reconnectInProgress = false;
        }
    }
};

void ptp_connect_failure(void *context, MQTTAsync_failureData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Connect Failure", ptp_mqtt->mqtt_id.c_str());
        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->connected = false;
            ptp_mqtt->reconnectInProgress = false;
        }
    }
};

void ptp_disconnect_success(void *context, MQTTAsync_successData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        INFO_LOG(AMULTIOS, "[%s] Disconnect Success", ptp_mqtt->mqtt_id.c_str());
        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->connected = false;
        }
    }
};

void ptp_disconnect_failure(void *context, MQTTAsync_failureData *response)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        ERROR_LOG(AMULTIOS, "[%s] Disconnect Failure", ptp_mqtt->mqtt_id.c_str());
        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->connected = false;
        }
    }
};

void ptp_connect_lost(void *context, char *cause)
{
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {
        WARN_LOG(AMULTIOS, "[%s] Connection Lost cause [%s]", ptp_mqtt->mqtt_id.c_str(), cause);
        {
            std::unique_lock<std::mutex> lk(ptp_mqtt_mutex);
            ptp_mqtt->connected = false;
        }
    }
};

int ptp_message_arrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    if (message)
    {
        auto ptp_mqtt = g_ptp_mqtt;
        // NOTICE_LOG(AMULTIOS, "[%s] got ptp message [%s] messagelen[%d] topic [%s] topiclen [%d]", ptr->mqtt_id.c_str(), (char *)message->payload, message->payloadlen, topicName, topicLen);
        if (ptp_mqtt != nullptr)
        {

            std::string topic = topicName;
            std::vector<std::string> topic_explode = explode(topic, '/');

            if (std::strcmp(topic_explode.at(1).c_str(), "DATA\0") == 0)
            {

                PTPMessage msg;
                msg.sport = std::stoi(topic_explode.at(5));
                getMac(&msg.sourceMac, topic_explode.at(4));
                msg.dport = std::stoi(topic_explode.at(3));
                getMac(&msg.destinationMac, topic_explode.at(2));
                msg.message = message;
                msg.topicName = topicName,
                msg.topicLen = topicLen;

                std::lock_guard<std::mutex> lock(ptp_queue_mutex);
                ptp_queue.push_back(msg);
                NOTICE_LOG(AMULTIOS, "[%s] PTP DATA message src [%s]:[%s] dst [%s]:[%s] messagelen[%d] topiclen [%d] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str(), message->payloadlen, topicLen);
            }

            if (std::strcmp(topic_explode.at(1).c_str(), "OPEN\0") == 0)
            {
                //VERBOSE_LOG(AMULTIOS, "[%s] PTP OPEN message src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                PTPConnection link;
                link.states = PTP_AMULTIOS_OPEN;
                link.sport = std::stoi(topic_explode.at(5));
                getMac(&link.sourceMac, topic_explode.at(4));
                link.dport = std::stoi(topic_explode.at(3));
                getMac(&link.destinationMac, topic_explode.at(2));

                std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                std::vector<PTPConnection>::iterator cit = std::find_if(ptp_peer_connection.begin(), ptp_peer_connection.end(), [&link](PTPConnection const &con) {
                    return (isSameMAC(&con.sourceMac, &link.sourceMac) && isSameMAC(&con.destinationMac, &link.destinationMac) && con.dport == link.dport && con.sport == link.sport);
                });

                if (cit == ptp_peer_connection.end())
                {
                    NOTICE_LOG(AMULTIOS, "[%s] PTP OPEN PUSHED src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                    ptp_peer_connection.push_back(link);
                }
                else
                {
                    NOTICE_LOG(AMULTIOS, "[%s] PTP OPEN STATES src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                    cit->states = PTP_AMULTIOS_OPEN;
                }
            }

            if (std::strcmp(topic_explode.at(1).c_str(), "CONNECT\0") == 0)
            {
                //VERBOSE_LOG(AMULTIOS, "[%s] PTP CONNECT message src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                PTPConnection link;
                link.states = PTP_AMULTIOS_CONNECT;
                link.sport = std::stoi(topic_explode.at(5));
                getMac(&link.sourceMac, topic_explode.at(4));
                link.dport = std::stoi(topic_explode.at(3));
                getMac(&link.destinationMac, topic_explode.at(2));

                std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                std::vector<PTPConnection>::iterator cit = std::find_if(ptp_peer_connection.begin(), ptp_peer_connection.end(), [&link](PTPConnection const &con) {
                    return (isSameMAC(&con.sourceMac, &link.sourceMac) && isSameMAC(&con.destinationMac, &link.destinationMac) && con.dport == link.dport && con.sport == link.sport);
                });

                if (cit == ptp_peer_connection.end())
                {
                    NOTICE_LOG(AMULTIOS, "[%s] PTP CONNECT PUSHED src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                    ptp_peer_connection.push_back(link);
                }
                else
                {
                    if (cit->states != PTP_AMULTIOS_ESTABLISHED)
                    {
                        NOTICE_LOG(AMULTIOS, "[%s] PTP CONNECT STATES src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        cit->states = PTP_AMULTIOS_CONNECT;
                    }
                }
            }

            if (std::strcmp(topic_explode.at(1).c_str(), "ACCEPT\0") == 0)
            {
                //VERBOSE_LOG(AMULTIOS, "[%s] PTP ACCEPT message src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                PTPConnection link;
                link.states = PTP_AMULTIOS_ACCEPT;
                link.sport = std::stoi(topic_explode.at(5));
                getMac(&link.sourceMac, topic_explode.at(4));
                link.dport = std::stoi(topic_explode.at(3));
                getMac(&link.destinationMac, topic_explode.at(2));

                std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                std::vector<PTPConnection>::iterator cit = std::find_if(ptp_peer_connection.begin(), ptp_peer_connection.end(), [&link](PTPConnection const &con) {
                    return (isSameMAC(&con.sourceMac, &link.sourceMac) && isSameMAC(&con.destinationMac, &link.destinationMac) && con.dport == link.dport && con.sport == link.sport);
                });

                if (cit == ptp_peer_connection.end())
                {
                    NOTICE_LOG(AMULTIOS, "[%s] PTP ACCEPT PUSHED src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                    ptp_peer_connection.push_back(link);
                }
                else
                {
                    NOTICE_LOG(AMULTIOS, "[%s] PTP ACCEPT STATES src [%s]:[%s] dst [%s]:[%s] ", ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                    if (cit->states != PTP_AMULTIOS_ESTABLISHED)
                    {
                        cit->states = PTP_AMULTIOS_ACCEPT;
                    }
                }
            }
        }
    }
    return 1;
};

int __AMULTIOS_CTL_INIT()
{
    int rc = MQTTASYNC_FAILURE;

    if (g_ctl_mqtt == nullptr)
    {
        ctlInited = true;
        g_ctl_mqtt = std::make_shared<AmultiosMqtt>();
        g_ctl_mqtt->subscribed = false;
        MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
        MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;

        g_ctl_mqtt->mqtt_id = "CTL/" + g_Config.sNickName;
        rc = MQTTAsync_create(&g_ctl_mqtt->client, ADDRESS, g_ctl_mqtt->mqtt_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
        MQTTAsync_setCallbacks(g_ctl_mqtt->client, NULL, ctl_connect_lost, ctl_message_arrived, NULL);

        opts.context = NULL;
        opts.keepAliveInterval = 300;
        opts.retryInterval = 0;
        opts.cleansession = 1;
        opts.connectTimeout = 20;
        opts.automaticReconnect = 1;
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

        g_ctl_mqtt->sub_topic = g_Config.sMACAddress + "/SceNetAdhocctl";
        g_ctl_mqtt->pub_topic = "SceNetAdhocctl";

        g_ctl_mqtt->reconnectInProgress = true;
        if ((rc = MQTTAsync_connect(g_ctl_mqtt->client, &opts)) != MQTTASYNC_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "Failed to connect, return code %d\n", rc);
        }

        {
            std::unique_lock<std::mutex> lk(ctl_running_mutex);
            ctl_running_cv.wait(lk, [] { return ctlRunning == false; });
        }
    }
    NOTICE_LOG(AMULTIOS, "ctl_mqtt Thread Finished");
    return rc;
}

int __AMULTIOS_CTL_SHUTDOWN()
{
    int rc = MQTTASYNC_SUCCESS;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        opts.onSuccess = ctl_disconnect_success;
        opts.onFailure = ctl_disconnect_failure;

        int i = 0;
        MQTTAsync_token *tokens;
        rc = MQTTAsync_getPendingTokens(ctl_mqtt->client, &tokens);
        if (rc == MQTTASYNC_SUCCESS && tokens)
        {
            while (tokens[i] != -1)
            {
                rc = MQTTAsync_waitForCompletion(ctl_mqtt->client, tokens[i], 5000L);
                MQTTAsync_free(tokens);
                ++i;
            }
        }

        int rc = MQTTAsync_disconnect(ctl_mqtt->client, &opts);
        while (ctl_mqtt->connected)
        {
            sleep_ms(1);
        }

        {
            std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            MQTTAsync_destroy(&ctl_mqtt->client);
            g_ctl_mqtt = nullptr;
            pdpInited = false;
        }

        NOTICE_LOG(AMULTIOS, "ctl_mqtt shutdown %d", rc);
        ctlInited = false;
    }
    return rc;
}

int __AMULTIOS_PDP_INIT()
{
    int rc = MQTTASYNC_FAILURE;
    if (g_pdp_mqtt == nullptr)
    {
        pdpInited = true;
        g_pdp_mqtt = std::make_shared<AmultiosMqtt>();
        g_pdp_mqtt->subscribed = 0;
        MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
        MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;

        g_pdp_mqtt->mqtt_id = "PDP/" + g_Config.sNickName;
        rc = MQTTAsync_create(&g_pdp_mqtt->client, ADDRESS, g_pdp_mqtt->mqtt_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
        MQTTAsync_setCallbacks(g_pdp_mqtt->client, NULL, pdp_connect_lost, pdp_message_arrived, NULL);

        opts.context = NULL;
        opts.keepAliveInterval = 300;
        opts.retryInterval = 0;
        opts.cleansession = 1;
        opts.connectTimeout = 20;
        //opts.maxInflight = 255;
        opts.automaticReconnect = 1;
        opts.onSuccess = pdp_connect_success;
        opts.onFailure = pdp_connect_failure;

        // initialize will message
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;

        will.message = (char *)&packet;
        will.topicName = "SceNetAdhocpdp";
        will.qos = 2;
        will.retained = 0;
        opts.will = &will;

        g_pdp_mqtt->reconnectInProgress = true;
        if ((rc = MQTTAsync_connect(g_pdp_mqtt->client, &opts)) != MQTTASYNC_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "Failed to connect, return code %d\n", rc);
        }

        {
            std::unique_lock<std::mutex> lk(pdp_running_mutex);
            pdp_running_cv.wait(lk, [] { return pdpRunning == false; });
        }
    }

    NOTICE_LOG(AMULTIOS, "pdp_mqtt Thread Finished");
    return rc;
}

int __AMULTIOS_PDP_SHUTDOWN()
{
    int rc = MQTTASYNC_SUCCESS;
    auto pdp_mqtt = g_pdp_mqtt;
    if (pdp_mqtt != nullptr)
    {

        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        opts.onSuccess = pdp_disconnect_success;
        //opts.onFailure = pdp_disconnect_failure;
        int rc = MQTTAsync_disconnect(pdp_mqtt->client, &opts);
        while (pdp_mqtt->connected)
        {
            sleep_ms(1);
        }

        {
            std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            MQTTAsync_destroy(&pdp_mqtt->client);
            g_pdp_mqtt = nullptr;
        }
        pdpInited = false;
        NOTICE_LOG(AMULTIOS, "pdp_mqtt shutdown %d", rc);
    }
    return rc;
}

int __AMULTIOS_PTP_INIT()
{
    int rc = MQTTASYNC_FAILURE;
    if (g_ptp_mqtt == nullptr)
    {
        ptpInited = true;
        g_ptp_mqtt = std::make_shared<AmultiosMqtt>();
        g_ptp_mqtt->subscribed = 0;
        MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
        MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;

        g_ptp_mqtt->mqtt_id = "PTP/" + g_Config.sNickName;
        rc = MQTTAsync_create(&g_ptp_mqtt->client, ADDRESS, g_ptp_mqtt->mqtt_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
        MQTTAsync_setCallbacks(g_ptp_mqtt->client, NULL, ptp_connect_lost, ptp_message_arrived, NULL);

        opts.context = NULL;
        opts.keepAliveInterval = 300;
        opts.retryInterval = 0;
        opts.cleansession = 1;
        opts.connectTimeout = 20;
        //opts.maxInflight = 255;
        opts.automaticReconnect = 1;
        opts.onSuccess = ptp_connect_success;
        opts.onFailure = ptp_connect_failure;

        // initialize will message
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;

        will.message = (char *)&packet;
        will.topicName = "SceNetAdhocptp";
        will.qos = 2;
        will.retained = 0;
        opts.will = &will;

        if ((rc = MQTTAsync_connect(g_ptp_mqtt->client, &opts)) != MQTTASYNC_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "Failed to connect, return code %d\n", rc);
        }

        {
            std::unique_lock<std::mutex> lk(ptp_running_mutex);
            ptp_running_cv.wait(lk, [] { return ptpRunning == false; });
        }
    }

    NOTICE_LOG(AMULTIOS, "ptp_mqtt Thread Finished");
    return rc;
}

int __AMULTIOS_PTP_SHUTDOWN()
{
    int rc = MQTTASYNC_SUCCESS;
    auto ptp_mqtt = g_ptp_mqtt;
    if (ptp_mqtt != nullptr)
    {

        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        opts.onSuccess = ptp_disconnect_success;
        //opts.onFailure = ptp_disconnect_failure;
        int rc = MQTTAsync_disconnect(ptp_mqtt->client, &opts);
        while (ptp_mqtt->connected)
        {
            sleep_ms(1);
        }

        {
            std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            MQTTAsync_destroy(&ptp_mqtt->client);
            g_ptp_mqtt = nullptr;
            ptpInited = false;
        }
        NOTICE_LOG(AMULTIOS, "ptp_mqtt shutdown %d", rc);
    }
    return rc;
}

int AmultiosNetAdhocInit()
{
    int rc = MQTTASYNC_FAILURE;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        rc = ctl_subscribe(ctl_mqtt->sub_topic.c_str(), 2);
    }
    return rc;
}

int AmultiosNetAdhocctlInit(SceNetAdhocctlAdhocId *adhoc_id)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        SceNetAdhocctlLoginPacketC2S packet;
        packet.base.opcode = OPCODE_LOGIN;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        strcpy((char *)packet.name.data, g_Config.sNickName.c_str());
        memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);
        return ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
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
                auto ctl_mqtt = g_ctl_mqtt;
                if (ctl_mqtt != nullptr && ctl_mqtt->connected)
                {
                    iResult = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
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
            auto ctl_mqtt = g_ctl_mqtt;
            if (ctl_mqtt != nullptr && ctl_mqtt->connected)
            {
                iResult = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
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
        auto ctl_mqtt = g_ctl_mqtt;
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
            iResult = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
        }
        // Clear Peer List
        freeFriendsRecursive(friends);
        INFO_LOG(AMULTIOS, "Cleared Peer List.");
        {
            std::lock_guard<std::mutex> lk(ptp_peer_mutex);
            ptp_peer_connection.clear();
        }

        // Delete Peer Reference
        friends = NULL;
    }
    return 0;
}

int AmultiosNetAdhocctlTerm()
{
    int rc = MQTTASYNC_FAILURE;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        rc = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 5000L);
        return rc;
    }

    return rc;
}

int AmultiosNetAdhocTerm()
{
    int rc;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        rc = ctl_unsubscribe(ctl_mqtt->sub_topic.c_str());
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
                if ((rc = pdp_subscribe(sub_topic.c_str(), 0) == MQTTASYNC_SUCCESS))
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
                                    //NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] PDP send topic single %s", pdp_single_topic.c_str());

                                    if (flag)
                                    {
                                        rc = pdp_publish(pdp_single_topic.c_str(), data, len, 0, 0);

                                        if (rc == MQTTASYNC_SUCCESS)
                                        {
                                            return 0;
                                        }
                                        return ERROR_NET_ADHOC_WOULD_BLOCK;
                                    }

                                    rc = pdp_publish(pdp_single_topic.c_str(), data, len, 1, timeout);

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
                                    std::string pdp_single_topic = "PDP/" + getMacString(&peer->mac_addr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport);

                                    if (flag)
                                    {
                                        rc = pdp_publish(pdp_single_topic.c_str(), data, len, 0, 0);
                                    }
                                    else
                                    {
                                        rc = pdp_publish(pdp_single_topic.c_str(), data, len, 1, timeout);
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

                    std::lock_guard<std::mutex> lk(pdp_queue_mutex);
                    std::vector<PDPMessage>::iterator it = std::find_if(pdp_queue.begin(), pdp_queue.end(), [&socket](PDPMessage const &obj) {
                        return (isSameMAC(&obj.destinationMac, &socket->laddr) && obj.dport == socket->lport);
                    });

                    if (it != pdp_queue.end())
                    {
                        PDPMessage packet = *it;
                        memcpy(buf, packet.message->payload, packet.message->payloadlen);
                        *saddr = packet.sourceMac;
                        *sport = (uint16_t)packet.sport;
                        //pdp_queuee Length
                        *len = packet.message->payloadlen;
                        MQTTAsync_freeMessage(&it->message);
                        MQTTAsync_free(it->topicName);
                        pdp_queue.erase(it);
                        return 0;
                    }
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
                int rc = pdp_unsubscribe(pdp_topic.at(id - 1).c_str());
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

int AmultiosNetAdhocPtpOpen(const char *srcmac, int sport, const char *dstmac, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int unknown)
{
    SceNetEtherAddr *saddr = (SceNetEtherAddr *)srcmac;
    SceNetEtherAddr *daddr = (SceNetEtherAddr *)dstmac;
    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Addresses
        if (saddr != NULL && isLocalMAC(saddr) && daddr != NULL && !isBroadcastMAC(daddr))
        {

            // Valid Ports
            if (!isPTPPortInUse(sport))
            {
                if (sport == 0)
                    sport = -(int)portOffset;
                // Valid Arguments
                if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0)
                {
                    // Create Infrastructure Socket
                    int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (tcpsocket > 0)
                    {
                        setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
                        sockaddr_in addr;
                        addr.sin_family = AF_INET;
                        addr.sin_addr.s_addr = INADDR_ANY;
                        addr.sin_port = htons(sport + portOffset);
                        if (bind(tcpsocket, (sockaddr *)&addr, sizeof(addr)) == 0)
                        {
                            socklen_t len = sizeof(addr);
                            if (getsockname(tcpsocket, (sockaddr *)&addr, &len) == 0)
                            {
                                sport = ntohs(addr.sin_port) - portOffset;
                            }

                            std::string sub_topic = "PTP/DATA/" + getMacString(saddr) + "/" + std::to_string(sport) + "/" + getMacString(daddr) + "/" + std::to_string(dport);
                            std::string pub_topic = "PTP/DATA/" + getMacString(daddr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(sport);
                            std::string open_topic = "PTP/ACCEPT/" + getMacString(saddr) + "/" + std::to_string(sport) + "/" + getMacString(daddr) + "/" + std::to_string(dport);
                            int rc = ptp_subscribe(open_topic.c_str(), 0);
                            rc = ptp_subscribe(sub_topic.c_str(), 0);
                            SceNetAdhocPtpStat *internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));

                            // Allocated Memory
                            if (internal != NULL && rc == MQTTASYNC_SUCCESS)
                            {
                                // Find Free Translator ID
                                int i = 0;
                                for (; i < 255; i++)
                                    if (ptp[i] == NULL)
                                        break;

                                // Found Free Translator ID
                                if (i < 255)
                                {
                                    // Clear Memory
                                    memset(internal, 0, sizeof(SceNetAdhocPtpStat));

                                    // Copy Infrastructure Socket ID
                                    internal->id = tcpsocket;

                                    // Copy Address Information
                                    internal->laddr = *saddr;
                                    internal->paddr = *daddr;
                                    internal->lport = sport;
                                    internal->pport = dport;

                                    // Set Buffer Size
                                    internal->rcv_sb_cc = bufsize;

                                    // Link PTP Socket
                                    ptp[i] = internal;
                                    ptp_relay_topic.at(i) = open_topic;
                                    ptp_sub_topic.at(i) = sub_topic;
                                    ptp_pub_topic.at(i) = pub_topic;
                                    NOTICE_LOG(SCENET, "PTP_OPEN_INTERNAL [%s]:[%d]->[%s]:[%d]", getMacString(&internal->laddr).c_str(), internal->lport, getMacString(&internal->paddr).c_str(), internal->pport);
                                    // Add Port Forward to Router
                                    // sceNetPortOpen("TCP", sport);
                                    // Return PTP Socket Pointer
                                    return i + 1;
                                }

                                // Free Memory
                                free(internal);
                            }
                        }
                    }
                }

                // Invalid Arguments
                return ERROR_NET_ADHOC_INVALID_ARG;
            }

            // Invalid Ports
            return ERROR_NET_ADHOC_INVALID_PORT;
        }

        // Invalid Addresses
        return ERROR_NET_ADHOC_INVALID_ADDR;
    }

    return 0;
}

int AmultiosNetAdhocPtpAccept(int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int flag)
{

    SceNetEtherAddr *addr = NULL;
    if (Memory::IsValidAddress(peerMacAddrPtr))
    {
        addr = PSPPointer<SceNetEtherAddr>::Create(peerMacAddrPtr);
    }
    uint16_t *port = NULL; //
    if (Memory::IsValidAddress(peerPortPtr))
    {
        port = (uint16_t *)Memory::GetPointer(peerPortPtr);
    }

    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Socket
        if (id > 0 && id <= 255 && ptp[id - 1] != NULL)
        {
            // Cast Socket
            SceNetAdhocPtpStat *sock = ptp[id - 1];

            // Listener Socket
            if (sock->state == ADHOC_PTP_STATE_LISTEN)
            {
                // Valid Arguments
                if (addr != NULL /*&& port != NULL*/)
                { //GTA:VCS seems to use 0 for the portPtr
                    // Address Information

                    bool found = false;
                    std::vector<PTPConnection>::iterator it;
                    {
                        std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                        it = ptp_peer_connection.begin();
                        while (it != ptp_peer_connection.end())
                        {
                            if (isSameMAC(&it->destinationMac, &sock->laddr) && it->dport == sock->lport && it->states == PTP_AMULTIOS_CONNECT)
                            {
                                found = true;
                                break;
                            }
                            it++;
                        }
                    }

                    // Blocking Behaviour
                    if (!flag && !found)
                    {
                        // Get Start Time
                        uint32_t starttime = (uint32_t)(real_time_now() * 1000000.0);

                        bool hasTime = (timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout);
                        // Retry until Timeout hits
                        while (hasTime && !found)
                        {
                            hasTime = (timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout);
                            std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                            it = ptp_peer_connection.begin();
                            while (hasTime && it != ptp_peer_connection.end())
                            {
                                if (isSameMAC(&it->destinationMac, &sock->laddr) && it->dport == sock->lport && it->states == PTP_AMULTIOS_CONNECT)
                                {
                                    found = true;
                                    break;
                                }
                                it++;
                            }
                            // Wait a bit...
                            sleep_ms(1);
                        }
                    }

                    if (found && macInNetwork(&it->sourceMac))
                    {

                        std::string sub_topic = "PTP/DATA/" + getMacString(&it->destinationMac) + "/" + std::to_string(it->dport) + "/" + getMacString(&it->sourceMac) + "/" + std::to_string(it->sport);
                        std::string accept_topic = "PTP/ACCEPT/" + getMacString(&it->sourceMac) + "/" + std::to_string(it->sport) + "/" + getMacString(&it->destinationMac) + "/" + std::to_string(it->dport);
                        // Allocate Memory
                        int rc = ptp_subscribe(sub_topic.c_str(), 0);
                        uint8_t send = PTP_AMULTIOS_ACCEPT;
                        rc = ptp_publish(accept_topic.c_str(), (void *)&send, sizeof(send), 1, 0);

                        int iResult = 0;
                        if (rc == MQTTASYNC_SUCCESS)
                        {
                            int tcpsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                            WARN_LOG(AMULTIOS, "[%s] accepting new socket", accept_topic.c_str());
                            SceNetAdhocPtpStat *internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));
                            // Allocated Memory
                            if (internal != NULL)
                            {
                                // Find Free Translator ID
                                int i = 0;
                                for (; i < 255; i++)
                                    if (ptp[i] == NULL)
                                        break;

                                // Found Free Translator ID
                                if (i < 255)
                                {

                                    // Copy Socket Descriptor to Structure
                                    internal->id = tcpsock;
                                    internal->rcv_sb_cc = sock->rcv_sb_cc;
                                    internal->snd_sb_cc = sock->snd_sb_cc;

                                    // Copy Local Address Data to Structure
                                    internal->laddr = it->destinationMac;
                                    internal->lport = it->dport;

                                    // Copy Peer Address Data to Structure
                                    internal->paddr = it->sourceMac;
                                    internal->pport = it->sport;

                                    // Set Connected State
                                    internal->state = ADHOC_PTP_STATE_ESTABLISHED;

                                    // Return Peer Address Information
                                    *addr = internal->paddr;
                                    if (port != NULL)
                                        *port = internal->pport;

                                    // Link PTP Socket
                                    ptp[i] = internal;

                                    {
                                        std::lock_guard<std::mutex> lk(ptp_peer_mutex);
                                        it->states = PTP_AMULTIOS_ESTABLISHED;
                                    }
                                    ptp_relay_topic.at(i) = accept_topic;
                                    ptp_sub_topic.at(i) = sub_topic;
                                    ptp_pub_topic.at(i) = "PTP/DATA/" + getMacString(&it->sourceMac) + "/" + std::to_string(it->sport) + "/" + getMacString(&it->destinationMac) + "/" + std::to_string(it->dport);
                                    // Add Port Forward to Router
                                    // sceNetPortOpen("TCP", internal->lport);

                                    INFO_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Established %s", id, sub_topic.c_str());

                                    // Return Socket
                                    return i + 1;
                                }

                                // Free Memory
                                free(internal);
                            }
                            closesocket(tcpsock);
                        }
                        ERROR_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Failed Accept Connection Not found", id);
                    }

                    // Action would block
                    if (flag)
                        return ERROR_NET_ADHOC_WOULD_BLOCK;

                    // Timeout
                    return ERROR_NET_ADHOC_TIMEOUT;
                }

                // Invalid Arguments
                return ERROR_NET_ADHOC_INVALID_ARG;
            }

            // Client Socket
            return ERROR_NET_ADHOC_NOT_LISTENED;
        }

        // Invalid Socket
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPtpConnect(int id, int timeout, int flag)
{
    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Socket
        if (id > 0 && id <= 255 && ptp[id - 1] != NULL)
        {
            // Cast Socket
            SceNetAdhocPtpStat *socket = ptp[id - 1];

            // Valid Client Socket
            if (socket->state == ADHOC_PTP_STATE_CLOSED)
            {
                // Grab Peer IP
                if (macInNetwork(&socket->paddr))
                {
                    std::string connect_topic = "PTP/CONNECT/" + getMacString(&socket->paddr) + "/" + std::to_string(socket->pport) + "/" + getMacString(&socket->laddr) + "/" + std::to_string(socket->lport);

                    // Connect Socket to Peer (Nonblocking)
                    uint8_t send = PTP_AMULTIOS_CONNECT;
                    int rc = ptp_publish(connect_topic.c_str(), (void *)&send, sizeof(send), 1, 0);

                    bool found = false;
                    std::vector<PTPConnection>::iterator it;
                    if (rc == MQTTASYNC_SUCCESS)
                    {
                        std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                        it = ptp_peer_connection.begin();
                        while (it != ptp_peer_connection.end())
                        {
                            if (isSameMAC(&it->destinationMac, &socket->laddr) && it->dport == socket->lport && it->states == PTP_AMULTIOS_ACCEPT)
                            {
                                found = true;
                                break;
                            }
                            it++;
                        }
                    }

                    if (found)
                    {
                        // Set Connected State
                        {
                            std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                            it->states = PTP_AMULTIOS_ESTABLISHED;
                        }

                        socket->state = ADHOC_PTP_STATE_ESTABLISHED;
                        NOTICE_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Already Connected", id, socket->lport);
                        // Success
                        return 0;
                    }

                    // Nonblocking Mode
                    if (flag)
                    {
                        return ERROR_NET_ADHOC_WOULD_BLOCK;
                    }

                    if (!flag && !found)
                    {
                        // Get Start Time
                        uint32_t starttime = (uint32_t)(real_time_now() * 1000000.0);

                        bool hasTime = (timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout);
                        // Retry until Timeout hits
                        while (hasTime && !found)
                        {
                            hasTime = (timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout);
                            std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                            it = ptp_peer_connection.begin();
                            while (hasTime && it != ptp_peer_connection.end())
                            {
                                if (isSameMAC(&it->destinationMac, &socket->laddr) && it->dport == socket->lport && it->states == PTP_AMULTIOS_ACCEPT)
                                {
                                    found = true;
                                    break;
                                }
                                it++;
                            }
                            // Wait a bit...
                            sleep_ms(1);
                        }
                    }

                    if (found)
                    {
                        // Set Connected State
                        {
                            std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                            it->states = PTP_AMULTIOS_ESTABLISHED;
                        }
                        socket->state = ADHOC_PTP_STATE_ESTABLISHED;
                        NOTICE_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Already Connected", id, socket->lport);
                        // Success
                        return 0;
                    }

                    return ERROR_NET_ADHOC_TIMEOUT;
                }
                // Peer not found
                return ERROR_NET_ADHOC_CONNECTION_REFUSED;
            }
            // Not a valid Client Socket
            return ERROR_NET_ADHOC_NOT_OPENED;
        }

        // Invalid Socket
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Library is uninitialized}
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPtpClose(int id, int unknown)
{

    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Arguments & Atleast one Socket
        if (id > 0 && id <= 255 && ptp[id - 1] != NULL)
        {
            // Cast Socket
            SceNetAdhocPtpStat *socket = ptp[id - 1];
            closesocket(socket->id);
            free(socket);
            if (!ptp_sub_topic.at(id - 1).empty())
            {
                ptp_unsubscribe(ptp_sub_topic.at(id - 1).c_str());
                ptp_sub_topic.at(id - 1).clear();
            }

            if (!ptp_relay_topic.at(id - 1).empty())
            {
                ptp_unsubscribe(ptp_relay_topic.at(id - 1).c_str());
                ptp_relay_topic.at(id - 1).clear();
            }

            // Free Reference
            ptp[id - 1] = NULL;

            // Success
            return 0;
        }

        // Invalid Argument
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPtpListen(const char *srcmac, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, int unk)
{

    // Library is initialized
    SceNetEtherAddr *saddr = (SceNetEtherAddr *)srcmac;
    if (netAdhocInited)
    {
        // Valid Address
        if (saddr != NULL && isLocalMAC(saddr))
        {

            // Valid Ports
            if (!isPTPPortInUse(sport))
            {
                // Valid Arguments
                if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0 && backlog > 0)
                {

                    // Create Infrastructure Socket
                    int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

                    // Valid Socket produced
                    if (tcpsocket > 0)
                    {

                        // Binding Information for local Port
                        sockaddr_in addr;
                        addr.sin_family = AF_INET;
                        addr.sin_addr.s_addr = INADDR_ANY;
                        addr.sin_port = htons(sport + portOffset);

                        int iResult = 0;
                        // Bound Socket to local Port
                        if ((iResult = bind(tcpsocket, (sockaddr *)&addr, sizeof(addr))) == 0)
                        {
                            // Switch into Listening Mode
                            std::string sub_topic = "PTP/CONNECT/" + getMacString(saddr) + "/" + std::to_string(sport) + "/#";
                            int rc = ptp_subscribe(sub_topic.c_str(), 0);
                            if ((iResult = listen(tcpsocket, backlog)) == 0 && rc == MQTTASYNC_SUCCESS)
                            {
                                SceNetAdhocPtpStat *internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));

                                // Allocated Memory
                                if (internal != NULL)
                                {

                                    // Find Free Translator ID
                                    int i = 0;
                                    for (; i < 255; i++)
                                        if (ptp[i] == NULL)
                                            break;

                                    // Found Free Translator ID
                                    if (i < 255)
                                    {
                                        // Clear Memory
                                        memset(internal, 0, sizeof(SceNetAdhocPtpStat));

                                        // Copy Infrastructure Socket ID
                                        internal->id = i;

                                        // Copy Address Information
                                        internal->laddr = *saddr;
                                        internal->lport = sport;

                                        // Flag Socket as Listener
                                        internal->state = ADHOC_PTP_STATE_LISTEN;

                                        // Set Buffer Size
                                        internal->rcv_sb_cc = bufsize;

                                        // Link PTP Socket
                                        ptp[i] = internal;
                                        ptp_sub_topic.at(i) = sub_topic;

                                        // Add Port Forward to Router
                                        // sceNetPortOpen("TCP", sport);

                                        // Return PTP Socket Pointer
                                        return i + 1;
                                    }

                                    // Free Memory
                                    free(internal);
                                }
                            }
                        }

                        if (iResult == SOCKET_ERROR)
                        {
                            int error = errno;
                            ERROR_LOG(SCENET, "sceNetAdhocPtpListen[%i]: Socket Error (%i)", sport, error);
                        }

                        // Close Socket
                        closesocket(tcpsocket);

                        // Port not available (exclusively in use?)
                        return ERROR_NET_ADHOC_PORT_NOT_AVAIL;
                    }
                    // Socket not available
                    return ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL;
                }

                // Invalid Arguments
                return ERROR_NET_ADHOC_INVALID_ARG;
            }

            // Invalid Ports
            return ERROR_NET_ADHOC_PORT_IN_USE;
        }

        // Invalid Addresses
        return ERROR_NET_ADHOC_INVALID_ADDR;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPtpSend(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag)
{

    int *len = (int *)Memory::GetPointer(dataSizeAddr);
    const char *data = Memory::GetCharPointer(dataAddr);
    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Socket
        if (id > 0 && id <= 255 && ptp[id - 1] != NULL)
        {
            // Cast Socket
            SceNetAdhocPtpStat *socket = ptp[id - 1];

            // Connected Socket
            if (socket->state == ADHOC_PTP_STATE_ESTABLISHED)
            {
                // Valid Arguments
                if (data != NULL && len != NULL && *len > 0)
                {

                    // Schedule Timeout Removal
                    if (flag)
                        timeout = 0;

                    int rc = ptp_publish(ptp_pub_topic.at(id - 1).c_str(), (void *)data, *len, 1, timeout);

                    // Success
                    if (rc == MQTTASYNC_SUCCESS)
                    {
                        // Save Length
                        *len = *len;
                        // Return Success
                        return 0;
                    }
                    else
                    {
                        // Blocking Situation
                        if (flag)
                            return ERROR_NET_ADHOC_WOULD_BLOCK;

                        // Timeout
                        return ERROR_NET_ADHOC_TIMEOUT;
                    }

                    // Change Socket State
                    socket->state = ADHOC_PTP_STATE_CLOSED;

                    // Disconnected
                    return ERROR_NET_ADHOC_DISCONNECTED;
                }

                // Invalid Arguments
                return ERROR_NET_ADHOC_INVALID_ARG;
            }

            // Not connected
            return ERROR_NET_ADHOC_NOT_CONNECTED;
        }

        // Invalid Socket
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int AmultiosNetAdhocPtpRecv(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag)
{

    void *buf = (void *)Memory::GetPointer(dataAddr);
    int *len = (int *)Memory::GetPointer(dataSizeAddr);
    // Library is initialized
    if (netAdhocInited)
    {
        // Valid Socket
        if (id > 0 && id <= 255 && ptp[id - 1] != NULL && ptp[id - 1]->state == ADHOC_PTP_STATE_ESTABLISHED)
        {
            // Cast Socket
            SceNetAdhocPtpStat *socket = ptp[id - 1];

            // Valid Arguments
            if (buf != NULL && len != NULL && *len > 0)
            {
                // Schedule Timeout Removal
                if (flag)
                {
                    timeout = 0;
                    std::lock_guard<std::mutex> lk(ptp_queue_mutex);
                    if (ptp_queue.size() > 0)
                    {
                        std::vector<PTPMessage>::iterator find = std::find_if(ptp_queue.begin(), ptp_queue.end(), [&socket](PTPMessage const &it) {
                            return (isSameMAC(&it.destinationMac, &socket->laddr) && it.dport == socket->lport && isSameMAC(&it.sourceMac, &socket->paddr) && it.sport == socket->pport);
                        });

                        if (find != ptp_queue.end())
                        {
                            PTPMessage packet = *find;
                            memcpy(buf, packet.message->payload, packet.message->payloadlen);
                            *len = packet.message->payloadlen;
                            MQTTAsync_freeMessage(&find->message);
                            MQTTAsync_free(find->topicName);
                            ptp_queue.erase(find);
                            return 0;
                        }
                    }
                    return ERROR_NET_ADHOC_WOULD_BLOCK;
                }
                else
                {

                    uint32_t starttime = (uint32_t)(real_time_now() * 1000000.0);

                    bool hasTime = (timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout);
                    // Retry until Timeout hits
                    while (hasTime)
                    {
                        hasTime = (timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout);

                        if (ptp_queue.size() > 0)
                        {
                            std::lock_guard<std::mutex> lk(ptp_queue_mutex);
                            std::vector<PTPMessage>::iterator find = std::find_if(ptp_queue.begin(), ptp_queue.end(), [&socket](PTPMessage const &it) {
                                return (isSameMAC(&it.destinationMac, &socket->laddr) && it.dport == socket->lport && isSameMAC(&it.sourceMac, &socket->paddr) && it.sport == socket->pport);
                            });

                            if (find != ptp_queue.end())
                            {
                                PTPMessage packet = *find;
                                memcpy(buf, packet.message->payload, packet.message->payloadlen);
                                *len = packet.message->payloadlen;
                                MQTTAsync_freeMessage(&find->message);
                                MQTTAsync_free(find->topicName);
                                ptp_queue.erase(find);
                                return 0;
                            }
                        }
                        // Wait a bit...
                        sleep_ms(1);
                    }
                    // Timeout
                    return ERROR_NET_ADHOC_TIMEOUT;
                }

                // Change Socket State
                socket->state = ADHOC_PTP_STATE_CLOSED;

                // Disconnected
                return ERROR_NET_ADHOC_DISCONNECTED;
            }

            // Invalid Arguments
            return ERROR_NET_ADHOC_INVALID_ARG;
        }

        // Invalid Socket
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Library is uninitialized
    return ERROR_NET_ADHOC_NOT_INITIALIZED;
}
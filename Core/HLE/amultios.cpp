#include "Core/Core.h"
#include "Core/Host.h"
#include "i18n/i18n.h"
#include "base/timeutil.h"
#include "amultios.h"
#include "util/text/parsers.h"
#include "snappy.h"

bool amultiosInited = false;
bool amultiosRunning = false;
std::thread amultiosThread;
std::shared_ptr<AmultiosMqtt> g_amultios_mqtt = nullptr;
struct mosquitto *amultios_client;
std::mutex amultios_mqtt_mutex;
std::string amultiosStatusTopic = "";

bool ctlInited = false;
bool ctlRunning = false;
std::thread ctlThread;
std::shared_ptr<AmultiosMqtt> g_ctl_mqtt = nullptr;
struct mosquitto *ctl_client;
std::mutex ctl_mqtt_mutex;
std::string ctlStatusTopic;

bool pdpInited = false;
bool pdpRunning = false;
std::thread pdpThread;
std::unique_ptr<AmultiosMqtt> g_pdp_mqtt = nullptr;
struct mosquitto *pdp_client;
std::mutex pdp_mqtt_mutex;

std::vector<std::string> pdp_topic(255);
std::mutex pdp_queue_mutex;
std::vector<PDPMessage> pdp_queue;

bool ptpInited = false;
bool ptpRunning = false;
std::thread ptpThread;

std::unique_ptr<AmultiosMqtt> g_ptp_mqtt = nullptr;
struct mosquitto *ptp_client;
std::mutex ptp_mqtt_mutex;

std::mutex ptp_queue_mutex;
std::vector<PTPMessage> ptp_queue;

std::vector<std::string> ptp_sub_topic(255, "");
std::vector<std::string> ptp_pub_topic(255, "");
std::vector<std::string> ptp_relay_topic(255, "");

std::mutex ptp_peer_mutex;
std::vector<PTPConnection> ptp_peer_connection;
int bindOffset = 0;

ChatMessages cmList;
LoginInfo loginInfo;
std::string g_token;
bool trusted = false;

void amultios_login()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr && amultios_mqtt->connected && amultiosInited)
    {

        AmultiosLoginPacket packet;
        memset(&packet, 0, sizeof(AmultiosLoginPacket));
        packet.base.opcode = OPCODE_AMULTIOS_LOGIN_PASS;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        strcpy((char *)packet.name.data, g_Config.sNickName.c_str());

        std::string pin = g_Config.sAmultiosPin.substr(0, 6);
        //INFO_LOG(AMULTIOS,"PIN length %s",pin.c_str() );
        strncpy((char *)packet.pin, pin.data(), pin.size());
        strcpy((char *)packet.revision, PPSSPP_GIT_VERSION);

        rc = amultios_subscribe(("AUTH/" + g_Config.sNickName).c_str(), 2);
        rc = amultios_subscribe(("TOKEN/" + g_Config.sNickName).c_str(), 2);
        rc = mosquitto_publish(amultios_client, NULL, "AUTH", sizeof(packet), &packet, 2, false);
        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(MQTT, "[%s] Login Error : %s", amultios_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
            cmList.Add("Cannot Contact Authorization Server", "Amultios", "SYSTEM", "HEADER");
        }
    }
}

void amultios_sync()
{
    loginInfo.logedIn = trusted;
    loginInfo.token = g_token;
    loginInfo.mac = g_Config.sMACAddress;
    loginInfo.nickname = g_Config.sNickName;
    if (g_amultios_mqtt != nullptr)
    {
        loginInfo.authServer = g_amultios_mqtt->connected;
    }

    if (g_ctl_mqtt != nullptr)
    {
        loginInfo.ctlServer = g_ctl_mqtt->connected;
    }

    if (g_pdp_mqtt != nullptr)
    {
        loginInfo.pdpServer = g_pdp_mqtt->connected;
    }

    if (g_ptp_mqtt != nullptr)
    {
        loginInfo.ptpServer = g_ptp_mqtt->connected;
    }

    loginInfo.party = getCurrentGroup();
    amultios_publish("TOKEN", (void *)g_token.data(), (int)g_token.length(), 2, 0);
}

void ChatMessages::doPlayerStatusUpdate()
{
    std::lock_guard<std::mutex> locker(playerStatusMutex);
    updatePlayerStatusFlag = false;
}

void ChatMessages::listenPlayerStatus()
{
    int rc = amultios_subscribe("STATUS", 2);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        ERROR_LOG(AMULTIOS, "Cannot listen into status error %s", mosquitto_strerror(rc));
    }
}

void ChatMessages::shutdownPlayerStatus()
{
    int rc = amultios_unsubscribe("STATUS");
    if (rc != MOSQ_ERR_SUCCESS)
    {
        ERROR_LOG(AMULTIOS, "Shutdown listen into status error %s", mosquitto_strerror(rc));
    }
}

bool ChatMessages::isMuted(const std::string &text)
{
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto isMuted = std::find(MuteList.begin(), MuteList.end(), upper);
    if (isMuted != MuteList.end())
    {
        return true;
    }
    return false;
}

bool ChatMessages::isAdmin(const std::string &text)
{
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto isAdmin = std::find(Admin.begin(), Admin.end(), upper);
    if (isAdmin != Admin.end())
    {
        return true;
    }
    return false;
}

bool ChatMessages::isChatScreenVisible()
{
    return chatScreenVisible;
}

void ChatMessages::doChatUpdate()
{
    std::lock_guard<std::mutex> locker(chatMutex_);
    updateChatFlag = false;
}

float ChatMessages::getLastUpdate()
{
    return lastUpdate;
}

void ChatMessages::Update()
{
    std::lock_guard<std::mutex> locker(chatMutex_);
    lastUpdate = time_now();
    updateChatFlag = true;
    updateOsmFlag = true;
}

void ChatMessages::Add(const std::string &text, const std::string &name, const std::string &room, const std::string &type, uint32_t namecolor)
{
    std::lock_guard<std::mutex> lk(chatMutex_);
    uint32_t green = 0x53C800;
    uint32_t red = 0x3643F4;
    uint32_t purple = 0xD893CE;
    uint32_t yellow = 0x45BFCA;
    uint32_t blue = 0xF39621;
    uint32_t white = 0xFFFFFF;
    //text coloring
    ChatMessage chat;
    // fill chat info;
    chat.name = name;
    chat.namecolor = namecolor;
    chat.room = room;
    chat.roomcolor = blue;
    chat.textcolor = white;
    chat.text = text;
    chat.type = type;

    if (room == "PARTY")
    {
        chat.roomcolor = green;
    }

    if (room == "PRIVATE")
    {
        chat.roomcolor = red;
    }

    if (room == "WORLD")
    {
        chat.roomcolor = purple;
    }

    if (name == g_Config.sNickName.c_str())
    {
        chat.namecolor = red;
    }

    if (isAdmin(name))
    {
        chat.namecolor = purple;
        if (name == "Amultios")
        {
            chat.roomcolor = yellow;
            if (type == "TEXT")
            {
                chat.textcolor = white;
            }
            else if (type == "HEADER")
            {
                chat.textcolor = yellow;
            }
        }
    }
    AllChatDb.push_back(chat);
}

void ChatMessages::ParseCommand(const std::string &text)
{
    std::vector<std::string> command(explode(text, ' '));
    if (command.size() == 0)
    {
        command.at(0) = text;
    }
    try
    {
        if (trusted)
        {
            if (command.at(0).at(0) == '!')
            {
                if (command.at(0) == "!help")
                {
                    cmList.Add("Command List", "Amultios", "SYSTEM", "HEADER");
                    cmList.Add("!help (display available command)", "Amultios", "SYSTEM");
                    cmList.Add("!tos (display Terms Of Service)");
                    cmList.Add("!list channel/sub/mute/party (display current list asked)", "Amultios", "SYSTEM");
                    cmList.Add("!mute nickname (mute someone)", "Amultios", "SYSTEM");
                    cmList.Add("!unmute nickname (unmute someone)", "Amultios", "SYSTEM");
                    cmList.Add("!sub channelname (listen chat on channel)", "Amultios", "SYSTEM");
                    cmList.Add("!unsub channelname (leave chat on channel)", "Amultios", "SYSTEM");
                    cmList.Add("!where (display your current chatroom)", "Amultios", "SYSTEM");
                    cmList.Add("!channel channelname (set chatroom into channel)", "Amultios", "SYSTEM");
                    cmList.Add("!pm nickname (set chatroom into private message with someone)", "Amultios", "SYSTEM");
                    //cmList.Add("!report nickname reason (report user for violation of TOS)", "Amultios", "SYSTEM");
                    cmList.Add("!clear sub/mute/message (clear the list of)");
                    //cmList.Add("@charactername/channel text (directly send your chat into channel)", "Amultios", "SYSTEM");
                }
                else if (command.at(0) == "!tos")
                {
                    cmList.Add("Amultios Terms Of Service", "Amultios", "SYSTEM", "HEADER");
                    cmList.Add("Use International English languange on WORLD chatroom");
                    cmList.Add("Speak your languange in their respective channel!");
                    cmList.Add("Respect each other");
                    cmList.Add("Do not ask for or link to pirated copyright content on community");
                    cmList.Add("Do not use downloaded save data from internet to play");
                    cmList.Add("Do not use Cheat or Cheated Save Data (Passive Cheat)");
                    cmList.Add("Use Official Amultios Emulator to play in our network");
                    cmList.Add("Read Amultios Faqs in main website before asking how to in community");
                    cmList.Add("Violation of TOS will cause your Amultios account banned and kicked!");
                }
                else if (command.at(0) == "!sub")
                {
                    std::string selected = command.at(1);
                    std::transform(selected.begin(), selected.end(), selected.begin(), ::toupper);
                    auto it = std::find(ChannelList.begin(), ChannelList.end(), selected);
                    if (it != ChannelList.end())
                    {
                        auto subit = std::find(SubcriptionList.begin(), SubcriptionList.end(), selected);
                        if (subit == SubcriptionList.end())
                        {
                            int rc = amultios_subscribe(("CHAT/" + selected + "/#").c_str(), 2);
                            if (rc == MOSQ_ERR_SUCCESS)
                            {
                                SubcriptionList.push_back(selected);
                                cmList.Add("Subscribe to channel " + selected + " success", "Amultios", "SYSTEM", "HEADER");
                            }
                            else
                            {
                                cmList.Add("Failed subscribe to channel " + selected, "Amultios", "SYSTEM", "HEADER");
                            }
                        }
                        else
                        {
                            cmList.Add("Already subscribed into channel " + selected, "Amultios", "SYSTEM", "HEADER");
                        }
                    }
                }
                else if (command.at(0) == "!unsub")
                {
                    std::string selected = command.at(1);
                    std::transform(selected.begin(), selected.end(), selected.begin(), ::toupper);
                    auto subit = std::find(SubcriptionList.begin(), SubcriptionList.end(), selected);
                    if (subit != SubcriptionList.end() && selected != "SYSTEM")
                    {
                        int rc = amultios_unsubscribe(("CHAT/" + selected + "/#").c_str());
                        if (rc == MOSQ_ERR_SUCCESS)
                        {
                            SubcriptionList.erase(subit);
                            cmList.Add("Unsubscribe to channel " + selected + " success", "Amultios", "SYSTEM", "HEADER");
                        }
                        else
                        {
                            cmList.Add("Failed unsubscribe to channel " + selected, "Amultios", "SYSTEM", "HEADER");
                        }
                    }
                    else
                    {
                        cmList.Add("Already not subscribed into channel " + selected, "Amultios", "SYSTEM", "HEADER");
                    }
                }
                else if (command.at(0) == "!mute")
                {
                    std::string original = command.at(1);

                    if (isAdmin(original))
                    {
                        cmList.Add("Sorry as bot i don't want to mute my boss");
                    }
                    else
                    {
                        std::string upper = command.at(1);
                        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                        auto muteit = std::find(MuteList.begin(), MuteList.end(), upper);
                        if (muteit == MuteList.end())
                        {
                            MuteList.push_back(upper);
                            displayMuteList.push_back(original);
                            cmList.Add("Muting " + original, "Amultios", "SYSTEM", "HEADER");
                        }
                        else
                        {
                            cmList.Add(original + " Already in mutelist", "Amultios", "SYSTEM", "HEADER");
                        }
                    }
                }
                else if (command.at(0) == "!unmute")
                {
                    std::string original = command.at(1);
                    std::string upper = command.at(1);
                    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                    auto muteit = std::find(MuteList.begin(), MuteList.end(), upper);
                    if (muteit != MuteList.end())
                    {
                        int index = std::distance(MuteList.begin(), muteit);
                        MuteList.erase(MuteList.begin() + index);
                        displayMuteList.erase(displayMuteList.begin() + index);
                        cmList.Add("Unmuting " + original, "Amultios", "SYSTEM", "HEADER");
                    }
                    else
                    {
                        cmList.Add(original + " Not in mutelist", "Amultios", "SYSTEM", "HEADER");
                    }
                }
                else if (command.at(0) == "!list")
                {
                    std::string selected = command.at(1);
                    std::transform(selected.begin(), selected.end(), selected.begin(), ::toupper);

                    if (selected == "CHANNEL")
                    {
                        std::string sublist;
                        for (const auto &piece : this->ChannelList)
                        {
                            sublist += piece;
                            sublist += ",";
                        }
                        sublist = sublist.substr(0, sublist.size() - 1);
                        cmList.Add("Available Channel", "Amultios", "SYSTEM", "HEADER");
                        cmList.Add(sublist);
                    }
                    else if (selected == "SUB")
                    {
                        std::string sublist;
                        int i = 0;
                        for (const auto &piece : this->SubcriptionList)
                        {
                            if (i > 2)
                            {
                                sublist += piece;
                                sublist += ",";
                            }
                            i++;
                        }
                        sublist = sublist.substr(0, sublist.size() - 1);
                        cmList.Add("Current Subscription List ", "Amultios", "SYSTEM", "HEADER");
                        cmList.Add(sublist);
                    }
                    else if (selected == "PARTY")
                    {
                    }
                    else if (selected == "MUTE")
                    {
                        std::string sublist;
                        for (const auto &piece : this->displayMuteList)
                        {
                            sublist += piece;
                            sublist += ",";
                        }
                        sublist = sublist.substr(0, sublist.size() - 1);
                        cmList.Add("Current Mute List ", "Amultios", "SYSTEM", "HEADER");
                        cmList.Add(sublist);
                    }
                    else
                    {
                        cmList.Add("Cannot list what you want options is channel,sub,party,mute");
                    }
                }
                else if (command.at(0) == "!where")
                {
                    cmList.Add(this->selectedRoom + " is your current chatroom", "Amultios", "SYSTEM", "HEADER");
                }
                else if (command.at(0) == "!channel")
                {
                    std::string selected = command.at(1);
                    std::transform(selected.begin(), selected.end(), selected.begin(), ::toupper);
                    if (selected == "PARTY")
                    {
                        this->selectedRoom = selected;
                        cmList.Add("Set Channel Into Party");
                    }
                    else
                    {
                        if (selected == "PRIVATE")
                        {
                            cmList.Add("use !pm nickname to switch into private room");
                        }
                        else
                        {
                            auto subit = std::find(SubcriptionList.begin(), SubcriptionList.end(), selected);
                            if (subit != SubcriptionList.end() && selected != "SYSTEM")
                            {
                                this->selectedRoom = selected;
                                cmList.Add("Set channel into " + this->selectedRoom + " success", "Amultios", "SYSTEM", "HEADER");
                            }
                            else
                            {
                                cmList.Add("Channel " + selected + " is not in your subscription list", "Amultios", "SYSTEM", "HEADER");
                            }
                        }
                    }
                }
                else if (command.at(0) == "!pm")
                {
                    this->privateMessageDisplay = command.at(1);
                    std::string nickname = command.at(1);
                    std::transform(nickname.begin(), nickname.end(), nickname.begin(), ::toupper);
                    this->selectedRoom = "PRIVATE";
                    this->privateMessageRoom = nickname;
                    cmList.Add("Set Room into private with " + this->privateMessageDisplay);
                }
                else if (command.at(0) == "!clear")
                {
                    std::string selected = command.at(1);
                    std::transform(selected.begin(), selected.end(), selected.begin(), ::toupper);
                    if (selected == "SUB")
                    {
                        this->SubcriptionList.resize(3);
                        cmList.Add("Subscription Cleared Success");
                    }
                    else if (selected == "MUTE")
                    {
                        this->MuteList.clear();
                        cmList.Add("Mute list Cleared Success");
                    }
                    else if (selected == "MESSAGE")
                    {
                        this->AllChatDb.clear();
                        cmList.Add("Clearing Message Success");
                    }
                }
                else
                {
                    cmList.Add("Unrecognized command type !help for valid command", "Amultios", "SYSTEM");
                    cmList.Add("If this command is available please report to admin for the bugs", "Amultios", "SYSTEM");
                }
            }
            else
            {
                cmList.SendChat(text);
            }
            cmList.Update();
        }
    }
    catch (const std::out_of_range)
    {
        ERROR_LOG(AMULTIOS, "Failed to parse command [%s]", text.c_str());
        cmList.Add("Invalid command pattern type !help for valid commandlist", "Amultios", "SYSTEM");
        cmList.Update();
    }
}

void ChatMessages::SendChat(const std::string &text)
{
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt->connected)
    {
        if (this->selectedRoom == "PRIVATE")
        {
            std::string pubtopic = "CHAT/" + this->selectedRoom + this->privateMessageRoom + "/" + g_Config.sNickName;
            int rc = amultios_publish(pubtopic.c_str(), (void *)text.c_str(), (int)text.size(), 2, 0);
            if (rc == MOSQ_ERR_SUCCESS)
            {
                cmList.Add(text, g_Config.sNickName, "PRIVATE");
            }
        }
        else if (this->selectedRoom == "PARTY")
        {
            std::string room = this->selectedRoom + getCurrentGroup();
            if (room == "PARTY")
            {
                cmList.Add("Party is Unavailable");
                cmList.Update();
            }
            else
            {
                std::string pubtopic = "CHAT/" + room + "/" + g_Config.sNickName;
                amultios_publish(pubtopic.c_str(), (void *)text.c_str(), (int)text.size(), 2, 0);
            }
        }
        else
        {
            auto subit = std::find(SubcriptionList.begin(), SubcriptionList.end(), this->selectedRoom);
            if (subit != SubcriptionList.end())
            {
                std::string topic = "CHAT/" + this->selectedRoom + "/" + g_Config.sNickName;
                amultios_publish(topic.c_str(), (void *)text.c_str(), (int)text.size(), 2, 0);
            }
            else
            {
                cmList.Add("Channel " + this->selectedRoom + " is not in your subscription list !sub channelname to subscribe");
                cmList.Update();
            }
        }
    }
}

void MqttTrace(void *level, char *message)
{
    INFO_LOG(MQTT, "%s", message);
}

std::string getCurrentGroup()
{
    char *gname = (char *)parameter.group_name.data;
    std::string s(gname, ADHOCCTL_GROUPNAME_LEN);
    s.erase(std::find(s.begin(), s.end(), '\0'), s.end());
    return s;
}

std::string getModeAddress()
{

    std::string mode = "amultios.net";
    if (g_Config.iAdhocMode == DEV_MODE)
    {
        mode = g_Config.proAdhocServer;
    }
    return mode;
}

void getBroadcastMAC(SceNetEtherAddr *addr)
{
    // Broadcast MAC
    memcpy(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN);
    //NOTICE_LOG(AMULTIOS,"MAC Checking %02x%02x%02x%02x%02x%02x",addr->data[0],addr->data[1],addr->data[2],addr->data[3],addr->data[4],addr->data[5]);
    //if (memcmp(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN) == 0) return true;
    // Normal MAC
    //return false;
}

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

std::string getMacString(const SceNetEtherAddr *addr)
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

bool macInNetwork(const SceNetEtherAddr *mac)
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

//start of amultios socket

int amultios_publish(const char *topic, void *payload, int size, int qos, unsigned long timeout)
{
    int rc = MOSQ_ERR_CONN_PENDING;
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr && amultios_mqtt->connected && amultiosInited)
    {

        //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
        rc = mosquitto_publish(amultios_client, NULL, topic, size, payload, qos, false);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(MQTT, "[%s] Publish Error : %s", amultios_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        amultios_mqtt->pub_topic_latest = topic;
        amultios_mqtt->pub_payload_len_latest = size;
        amultios_mqtt->qos_latest = qos;
    }
    return rc;
}

int amultios_subscribe(const char *topic, int qos)
{
    auto amultios_mqtt = g_amultios_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (amultios_mqtt != nullptr && amultios_mqtt->connected && amultiosInited)
    {
        NOTICE_LOG(AMULTIOS, "Amultios Game Info topic %s", topic);
        {
            //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
            amultios_mqtt->sub_topic_latest = topic;
            amultios_mqtt->qos_latest = qos;
        }

        rc = mosquitto_subscribe(amultios_client, NULL, topic, qos);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] error %s", amultios_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }
    }
    return rc;
}

int amultios_unsubscribe(const char *topic)
{
    auto amultios_mqtt = g_amultios_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (amultios_mqtt != nullptr && amultios_mqtt->connected && amultiosInited)
    {
        {
            //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
            amultios_mqtt->sub_topic_latest = topic;
        }
        rc = mosquitto_unsubscribe(amultios_client, NULL, topic);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] error %s", amultios_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }
    }
    return rc;
}

void amultios_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr && amultiosInited)
    {
        VERBOSE_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%u] qos [%d] ", amultios_mqtt->mqtt_id.c_str(), amultios_mqtt->pub_topic_latest.c_str(), (uint32_t)amultios_mqtt->pub_payload_len_latest, amultios_mqtt->qos_latest);
    }
};

void amultios_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr && amultiosInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", amultios_mqtt->mqtt_id.c_str(), amultios_mqtt->sub_topic_latest.c_str(), amultios_mqtt->qos_latest);
        {
            //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
            amultios_mqtt->subscribed += 1;
        }
    }
};

void amultios_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid)
{
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr && amultiosInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", amultios_mqtt->mqtt_id.c_str(), amultios_mqtt->sub_topic_latest.c_str());
        {
            //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
            amultios_mqtt->subscribed -= 1;
        }
    }
};

void amultios_connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr)
    {
        cmList.Add("Connected into Amultios Network", "Amultios", "SYSTEM", "HEADER");
        NOTICE_LOG(AMULTIOS, "[%s] Connect Success", amultios_mqtt->mqtt_id.c_str());
        {
            //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
            amultios_mqtt->connected = true;
            amultios_mqtt->reconnectInProgress = false;
        }
        amultios_login();
    }
};

void amultios_disconnect_callback(struct mosquitto *mosq, void *obj)
{
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultios_mqtt != nullptr)
    {
        NOTICE_LOG(AMULTIOS, "[%s] Disconnect Success", amultios_mqtt->mqtt_id.c_str());
        //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
        amultios_mqtt->connected = false;
        amultios_mqtt->disconnectComplete = true;
    }
};

void amultios_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    if (message && amultiosInited)
    {
        std::string topic = message->topic;
        try
        {
            if (topic == "STATUS")
            {
                char *data = (char *)message->payload;
                std::string playerStatus(data, data + message->payloadlen);
                cmList.setStatus(playerStatus);
            }
            else
            {
                std::vector<std::string> topic_explode(explode(topic, '/'));
                if (topic_explode.at(0) == "CHAT")
                {
                    char *data = (char *)message->payload;
                    std::string chatMessage(data, data + message->payloadlen);

                    if (!cmList.isMuted(topic_explode.at(2)))
                    {

                        INFO_LOG(AMULTIOS, "Chat Topic %s", topic_explode.at(1).c_str());
                        if (topic_explode.at(1).find("PARTY") != std::string::npos)
                        {
                            cmList.Add(chatMessage, topic_explode.at(2), "PARTY");
                            cmList.Update();
                        }
                        else if (topic_explode.at(1).find("PRIVATE") != std::string::npos)
                        {
                            cmList.Add(chatMessage, topic_explode.at(2), "PRIVATE");
                            cmList.Update();
                        }
                        else
                        {
                            cmList.Add(chatMessage, topic_explode.at(2), topic_explode.at(1));
                            cmList.Update();
                        }
                    }
                }
                else if (topic_explode.at(0) == "AUTH")
                {
                    char *data = (char *)message->payload;
                    std::string token(data, data + message->payloadlen);
                    if (token.at(0) == OPCODE_AMULTIOS_LOGIN_PASS)
                    {
                        trusted = true;
                        cmList.Add(token.substr(1, 64), "Amultios", "SYSTEM", "HEADER");
                        cmList.Add("Type !help for list of command", "Amultios", "SYSTEM", "TEXT");
                        cmList.ParseCommand("!tos");
                        cmList.ParseCommand("!sub world");
                        std::string up = g_Config.sNickName;
                        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                        int rc = amultios_subscribe(("CHAT/PRIVATE" + up + "/#").c_str(), 2);
                        if (rc == MOSQ_ERR_SUCCESS)
                        {
                            cmList.Add("Private chatroom is " + g_Config.sNickName);
                        }
                    }
                    else
                    {
                        trusted = false;
                        cmList.Add("Disconnected From Network", "Amultios", "SYSTEM", "HEADER");
                        cmList.Add(token.substr(1, 64), "Amultios", "SYSTEM", "HEADER");
                        cmList.Update();
                    }
                }
                else if (topic_explode.at(0) == "TOKEN")
                {
                    char *data = (char *)message->payload;
                    std::string token(data, data + message->payloadlen);
                    g_token = token;
                    amultios_sync();
                }
            }
        }
        catch (const std::out_of_range)
        {
            ERROR_LOG(AMULTIOS, "Failed To Parse chat Message Topic[%s]", message->topic);
        }
    }
};

//start of ctl relay
int ctl_publish(const char *topic, void *payload, int size, int qos, unsigned long timeout)
{
    int rc = MOSQ_ERR_CONN_PENDING;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected && ctlInited)
    {

        rc = mosquitto_publish(ctl_client, NULL, topic, size, payload, qos, false);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(MQTT, "[%s] publish error : %s", ctl_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }
    }
    return rc;
}

int ctl_subscribe(const char *topic, int qos)
{
    auto ctl_mqtt = g_ctl_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected && ctlInited)
    {
        NOTICE_LOG(AMULTIOS, "[%s] subscribe to topic:[%s] qos:[%d]", ctl_mqtt->mqtt_id.c_str(), topic, qos);
        {
            //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->sub_topic_latest = topic;
            ctl_mqtt->qos_latest = qos;
        }
        rc = mosquitto_subscribe(ctl_client, NULL, topic, qos);
    }
    return rc;
}

int ctl_unsubscribe(const char *topic)
{
    auto ctl_mqtt = g_ctl_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected && ctlInited)
    {
        {
            //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->sub_topic_latest = topic;
        }
        rc = mosquitto_unsubscribe(ctl_client, NULL, topic);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] unsubscribe error %s", ctl_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }
    }
    return rc;
}

void ctl_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctlInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%u] qos [%d] ", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->pub_topic_latest.c_str(), (uint32_t)ctl_mqtt->pub_payload_len_latest, ctl_mqtt->qos_latest);
    }
};

void ctl_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctlInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->sub_topic_latest.c_str(), ctl_mqtt->qos_latest);
        //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
        ctl_mqtt->subscribed += 1;
    }
};

void ctl_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctlInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", ctl_mqtt->mqtt_id.c_str(), ctl_mqtt->sub_topic_latest.c_str());
        //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
        ctl_mqtt->subscribed -= 1;
    }
};

void ctl_connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        NOTICE_LOG(AMULTIOS, "[%s] Connect Success", ctl_mqtt->mqtt_id.c_str());
        {
            //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            ctl_mqtt->connected = true;
            ctl_mqtt->reconnectInProgress = false;
        }
    }
};

void ctl_disconnect_callback(struct mosquitto *mosq, void *obj)
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr)
    {
        //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
        NOTICE_LOG(AMULTIOS, "[%s] Disconnect Success", ctl_mqtt->mqtt_id.c_str());
        //threadStatus = ADHOCCTL_STATE_DISCONNECTED;
        ctl_mqtt->connected = false;
        //MQTTAsync_destroy(&ctl_mqtt->client);
        ctl_mqtt->disconnectComplete = true;
    }
    ctlInited = false;
};

void ctl_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    if (message && ctlInited)
    {
        char *payload_ptr = static_cast<char *>(message->payload);
        if (payload_ptr[0] == OPCODE_CONNECT_BSSID)
        {
            NOTICE_LOG(AMULTIOS, "[CTL_NETWORK] Incoming connect BSSID");
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
            //group->
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
        else if (payload_ptr[0] == OPCODE_AMULTIOS_LOGIN_PASS)
        {
            AmultiosNetAdhocctlLoginPacketS2C *packet = (AmultiosNetAdhocctlLoginPacketS2C *)payload_ptr;
            ctlStatusTopic = "AmultiosSynchronize/";
            ctlStatusTopic.append(packet->game.data);
            //amultios_subscribe(ctlStatusTopic.c_str(), 2);
        }
    }
};

//start of pdp relay
int pdp_publish(std::string topic, std::string payload, int size, int qos, unsigned long timeout)
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (g_pdp_mqtt != nullptr && g_pdp_mqtt->connected && pdpInited)
    {
        if (!topic.empty() && !payload.empty())
        {
            rc = mosquitto_publish(pdp_client, NULL, topic.c_str(), size, payload.data(), qos, false);
        }
    }
    return rc;
}

int pdp_subscribe(const char *topic, int qos)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (g_pdp_mqtt != nullptr && g_pdp_mqtt->connected && pdpInited)
    {
        NOTICE_LOG(AMULTIOS, "[%s] subscribe to topic:[%s] qos:[%d]", g_pdp_mqtt->mqtt_id.c_str(), topic, qos);
        {
            //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            g_pdp_mqtt->sub_topic_latest = topic;
            g_pdp_mqtt->qos_latest = qos;
        }
        rc = mosquitto_subscribe(pdp_client, NULL, topic, qos);
    }
    return rc;
}

int pdp_unsubscribe(const char *topic)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (g_pdp_mqtt != nullptr && g_pdp_mqtt->connected && pdpInited)
    {
        rc = mosquitto_unsubscribe(pdp_client, NULL, topic);
        //{
        //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        //pdp_mqtt->sub_topic_latest = topic;
        //}
    }
    return rc;
}

void pdp_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    if (g_pdp_mqtt != nullptr && pdpInited)
    {
        /*auto it = pdp_oque.begin();

        while(it != pdp_oque.end()){

			if (it. == || mid == 0) {
				delete *it;
				pdp_oque.erase(it);
			}

			it++;
        }*/
        VERBOSE_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%u] qos [%d] ", g_pdp_mqtt->mqtt_id.c_str(), g_pdp_mqtt->pub_topic_latest.c_str(), (uint8_t)g_pdp_mqtt->pub_payload_len_latest, g_pdp_mqtt->qos_latest);
    }
};

void pdp_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    if (g_pdp_mqtt != nullptr && pdpInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", g_pdp_mqtt->mqtt_id.c_str(), g_pdp_mqtt->sub_topic_latest.c_str(), g_pdp_mqtt->qos_latest);
        //{
        //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        //pdp_mqtt->subscribed += 1;
        //}
    }
};

void pdp_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    if (g_pdp_mqtt != nullptr && pdpInited)
    {
        INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", g_pdp_mqtt->mqtt_id.c_str(), g_pdp_mqtt->sub_topic_latest.c_str());
        {
            // std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            //pdp_mqtt->subscribed -= 1;
        }
    }
};

void pdp_connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    if (g_pdp_mqtt != nullptr)
    {
        NOTICE_LOG(AMULTIOS, "[%s] Connect Success", g_pdp_mqtt->mqtt_id.c_str());
        {
            //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            g_pdp_mqtt->connected = true;
            g_pdp_mqtt->reconnectInProgress = false;
        }
    }
};

void pdp_disconnect_callback(struct mosquitto *mosq, void *obj)
{
    //auto pdp_mqtt = g_pdp_mqtt;
    if (g_pdp_mqtt != nullptr)
    {
        NOTICE_LOG(AMULTIOS, "[%s] Disconnect Success", g_pdp_mqtt->mqtt_id.c_str());
        {
            //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            g_pdp_mqtt->connected = false;
            g_pdp_mqtt->disconnectComplete = true;
        }
    }
};

void pdp_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    if (message && pdpInited)
    {
        std::string topic = message->topic;
        try
        {
            std::vector<std::string> topic_explode(explode(topic, '/'));
            PDPMessage msg;
            msg.sport = std::stoi(topic_explode.at(5));
            getMac(&msg.sourceMac, topic_explode.at(4));
            msg.dport = std::stoi(topic_explode.at(3));
            getMac(&msg.destinationMac, topic_explode.at(2));
            msg.payloadlen = message->payloadlen;

            char *data = (char *)message->payload;
            std::string input(data, data + (int)msg.payloadlen);

            bool success = snappy::Uncompress(input.data(), input.size(), &msg.payload);

            if (success)
            {
                msg.payloadlen = (int)msg.payload.length();
                //msg.payload = std::vector<char>(data, data + msg.payloadlen);
                //DEBUG_LOG(AMULTIOS, "PDP Message Received len:[%d]", msg.payloadlen);
                {
                    std::lock_guard<std::mutex> lock(pdp_queue_mutex);
                    pdp_queue.push_back(msg);
                }
            }
        }
        catch (const std::out_of_range)
        {
            ERROR_LOG(AMULTIOS, "Failed To Parse PDP Message Topic[%s]", message->topic);
        }
    }
};

// start of ptp relay
int ptp_publish(std::string topic, std::string payload, int size, int qos, unsigned long timeout)
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (g_ptp_mqtt != nullptr && g_ptp_mqtt->connected && ptpInited)
    {
        if (!topic.empty() && !payload.empty())
        {
            rc = mosquitto_publish(ptp_client, NULL, topic.c_str(), size, payload.data(), qos, false);
        }
    }
    return rc;
}

int ptp_subscribe(const char *topic, int qos)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (g_ptp_mqtt != nullptr && g_ptp_mqtt->connected && ptpInited)
    {
        //std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
        rc = mosquitto_subscribe(ptp_client, NULL, topic, qos);
        INFO_LOG(AMULTIOS, "ptp_mqtt subscribe to topic:[%s] qos:[%d]", topic, qos);
        g_ptp_mqtt->sub_topic_latest = topic;
        g_ptp_mqtt->qos_latest = qos;
    }
    return rc;
}

int ptp_unsubscribe(const char *topic)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    int rc = MOSQ_ERR_CONN_PENDING;
    if (g_ptp_mqtt != nullptr && g_ptp_mqtt->connected && ptpInited)
    {
        INFO_LOG(AMULTIOS, "ptp_mqtt unsubcribe to topic:[%s]", topic);
        rc = mosquitto_unsubscribe(ptp_client, NULL, topic);
    }
    return rc;
}

void ptp_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    if (g_ptp_mqtt != nullptr && ptpInited)
    {
        //auto it = std::find_if(ptp_oque.begin(),ptp_oque.end(),[&mid](const OutQueue &q){ return q.mid == mid;});
        //if(it != ptp_oque.end()){
        //ptp_oque.erase(it);
        //}
        VERBOSE_LOG(AMULTIOS, "[%s] Publish Success on topic [%s] payload_len [%u] qos [%d] ", g_ptp_mqtt->mqtt_id.c_str(), g_ptp_mqtt->pub_topic_latest.c_str(), (uint32_t)g_ptp_mqtt->pub_payload_len_latest, g_ptp_mqtt->qos_latest);
    }
};

void ptp_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    if (g_ptp_mqtt != nullptr && ptpInited)
    {
        //INFO_LOG(AMULTIOS, "[%s] Subscribe Success on topic [%s] qos [%d]", ptp_mqtt->mqtt_id.c_str(), ptp_mqtt->sub_topic_latest.c_str(), ptp_mqtt->qos_latest);
        //std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
        g_ptp_mqtt->subscribed += 1;
    }
};

void ptp_unsubscribe_callback(struct mosquitto *mosq, void *obj, int mid)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    if (g_ptp_mqtt != nullptr && ptpInited)
    {
        //INFO_LOG(AMULTIOS, "[%s] Unsubscribe Success on topic [%s]", ptp_mqtt->mqtt_id.c_str(), ptp_mqtt->sub_topic_latest.c_str());
        //std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
        g_ptp_mqtt->subscribed -= 1;
    }
};

void ptp_connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    if (g_ptp_mqtt != nullptr)
    {
        NOTICE_LOG(AMULTIOS, "[%s] Connect Success", g_ptp_mqtt->mqtt_id.c_str());
        {
            //std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            g_ptp_mqtt->connected = true;
            g_ptp_mqtt->reconnectInProgress = false;
        }
    }
};

void ptp_disconnect_callback(struct mosquitto *mosq, void *obj)
{
    //auto ptp_mqtt = g_ptp_mqtt;
    if (g_ptp_mqtt != nullptr)
    {
        //std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
        NOTICE_LOG(AMULTIOS, "[%s] Disconnect Success", g_ptp_mqtt->mqtt_id.c_str());
        g_ptp_mqtt->connected = false;
        g_ptp_mqtt->disconnectComplete = true;
        //MQTTAsync_destroy(&ptp_mqtt->client);
    }
};

void ptp_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    if (message && ptpInited)
    {
        //auto ptp_mqtt = g_ptp_mqtt;
        // NOTICE_LOG(AMULTIOS, "[%s] got ptp message [%s] messagelen[%d] topic [%s] topiclen [%d]", ptr->mqtt_id.c_str(), (char *)message->payload, message->payloadlen, topicName, topicLen);
        if (g_ptp_mqtt != nullptr)
        {

            std::string topic = message->topic;
            try
            {
                std::vector<std::string> topic_explode(explode(topic, '/'));
                if (std::strcmp(topic_explode.at(1).c_str(), "D\0") == 0)
                {

                    PTPMessage msg;
                    msg.sport = std::stoi(topic_explode.at(5));
                    getMac(&msg.sourceMac, topic_explode.at(4));
                    msg.dport = std::stoi(topic_explode.at(3));
                    getMac(&msg.destinationMac, topic_explode.at(2));
                    //msg.payloadlen = message->payloadlen;

                    char *data = (char *)message->payload;
                    std::string input(data, data + (int)message->payloadlen);

                    bool success = snappy::Uncompress(input.data(), input.size(), &msg.payload);

                    if (success)
                    {
                        msg.payloadlen = static_cast<int>(msg.payload.length());
                        {
                            INFO_LOG(AMULTIOS, "[%s] PTP DATA message src [%s]:[%s] dst [%s]:[%s] messagelen[%d] topiclen [%d] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str(), message->payloadlen, (int)topic.length());
                            std::lock_guard<std::mutex> lock(ptp_queue_mutex);
                            ptp_queue.push_back(msg);
                        }
                    }
                }

                if (std::strcmp(topic_explode.at(1).c_str(), "O\0") == 0)
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
                        INFO_LOG(AMULTIOS, "[%s] PTP OPEN PUSHED src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        ptp_peer_connection.push_back(link);
                    }
                    else
                    {
                        if (cit->states != PTP_AMULTIOS_ESTABLISHED)
                        {
                            VERBOSE_LOG(AMULTIOS, "[%s] PTP OPEN STATES src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                            cit->states = PTP_AMULTIOS_OPEN;
                        }
                        else
                        {
                            WARN_LOG(AMULTIOS, "[%s] PTP OPEN STATES already ESTABLISHED src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        }
                    }
                }

                if (std::strcmp(topic_explode.at(1).c_str(), "C\0") == 0)
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
                        INFO_LOG(AMULTIOS, "[%s] PTP CONNECT PUSHED src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        ptp_peer_connection.push_back(link);
                    }
                    else
                    {
                        if (cit->states != PTP_AMULTIOS_ESTABLISHED)
                        {
                            VERBOSE_LOG(AMULTIOS, "[%s] PTP CONNECT STATES src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                            cit->states = PTP_AMULTIOS_CONNECT;
                        }
                        else
                        {
                            WARN_LOG(AMULTIOS, "[%s] PTP CONNECT STATES Already Established src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        }
                    }
                }

                if (std::strcmp(topic_explode.at(1).c_str(), "A\0") == 0)
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
                        INFO_LOG(AMULTIOS, "[%s] PTP ACCEPT PUSHED src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        ptp_peer_connection.push_back(link);
                    }
                    else
                    {
                        if (cit->states != PTP_AMULTIOS_ESTABLISHED)
                        {
                            VERBOSE_LOG(AMULTIOS, "[%s] PTP ACCEPT STATES src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                            cit->states = PTP_AMULTIOS_ACCEPT;
                        }
                        else
                        {
                            WARN_LOG(AMULTIOS, "[%s] PTP ACCEPT STATES already ESTABLISHED src [%s]:[%s] dst [%s]:[%s] ", g_ptp_mqtt->mqtt_id.c_str(), topic_explode.at(4).c_str(), topic_explode.at(5).c_str(), topic_explode.at(2).c_str(), topic_explode.at(3).c_str());
                        }
                    }
                }
            }
            catch (const std::out_of_range)
            {
                ERROR_LOG(AMULTIOS, "Failed To Parse PTP Message Topic[%s]", message->topic);
            }
        }
    }
};

int __AMULTIOS_START()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (amultiosInited)
    {
        auto amultios_mqtt = g_amultios_mqtt;
        rc = mosquitto_loop_forever(amultios_client, 1000, 1);
    }
    return rc;
}

int __AMULTIOS_CTL_START()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (ctlInited)
    {
        auto ctl_mqtt = g_ctl_mqtt;
        rc = mosquitto_loop_forever(ctl_client, 100, 1);
    }
    return rc;
}

int __AMULTIOS_PDP_START()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (pdpInited)
    {
        //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
        //auto pdp_mqtt = g_pdp_mqtt;
        rc = mosquitto_loop_forever(pdp_client, 1, 1);
    }
    return rc;
}

int __AMULTIOS_PTP_START()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (ptpInited)
    {
        //auto ptp_mqtt = g_ptp_mqtt;
        rc = mosquitto_loop_forever(ptp_client, 1, 1);
    }
    return rc;
}

int __AMULTIOS_INIT()
{

    int rc = MOSQ_ERR_CONN_PENDING;
    if (!amultiosInited)
    {
        amultiosInited = true;
        {
            //std::lock_guard<std::mutex> lk(amultios_mqtt_mutex);
            g_amultios_mqtt = std::make_shared<AmultiosMqtt>();
            g_amultios_mqtt->subscribed = 0;
            g_amultios_mqtt->mqtt_id = "AMULTIOS/" + g_Config.sNickName + "/" + g_Config.sMACAddress;
            amultios_client = mosquitto_new(g_amultios_mqtt->mqtt_id.c_str(), true, NULL);
        }

        mosquitto_connect_callback_set(amultios_client, amultios_connect_callback);
        mosquitto_publish_callback_set(amultios_client, amultios_publish_callback);
        mosquitto_subscribe_callback_set(amultios_client, amultios_subscribe_callback);
        mosquitto_unsubscribe_callback_set(amultios_client, amultios_unsubscribe_callback);
        mosquitto_message_callback_set(amultios_client, amultios_message_callback);
        mosquitto_threaded_set(amultios_client, true);

        // initialize will message
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        mosquitto_will_set(amultios_client, "Amultios", sizeof(packet), &packet, 2, false);

        g_amultios_mqtt->sub_topic = "Amultios/" + g_Config.sMACAddress;
        g_amultios_mqtt->pub_topic = "Amultios";
        g_amultios_mqtt->reconnectInProgress = true;

        if ((rc = mosquitto_connect(amultios_client, getModeAddress().c_str(), 1883, 60)) != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to connect, return code %s\n", g_amultios_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //rc = mosquitto_loop_start(g_amultios_mqtt->mclient);

        //if (rc == MOSQ_ERR_SUCCESS)
        //{
        //g_amultios_mqtt->ownThread = false;
        //}

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to start loop %s\n", g_amultios_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //if (rc == MOSQ_ERR_NOT_SUPPORTED)
        //{
        WARN_LOG(AMULTIOS, "[%s] Running own thread ", g_amultios_mqtt->mqtt_id.c_str());
        g_amultios_mqtt->ownThread = true;
        amultiosThread = std::thread(__AMULTIOS_START);
        //}
    }
    return rc;
}

int __AMULTIOS_SHUTDOWN()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    auto amultios_mqtt = g_amultios_mqtt;
    if (amultiosInited)
    {
        cmList.ParseCommand("!clear sub");
        rc = mosquitto_disconnect(amultios_client);
        //mosquitto_loop_stop(amultios_client, false);

        if (amultios_mqtt->ownThread && amultiosThread.joinable())
        {
            amultiosThread.join();
        }

        mosquitto_destroy(amultios_client);
        amultiosInited = false;
        NOTICE_LOG(AMULTIOS, "amultios_mqtt shutdown %d", rc);
    }
    NOTICE_LOG(AMULTIOS, "amultios_mqtt Thread Finished");
    return rc;
}

int __AMULTIOS_CTL_INIT()
{
    int rc = MOSQ_ERR_CONN_PENDING;

    if (!ctlInited)
    {
        ctlInited = true;
        {
            //std::lock_guard<std::mutex> lk(ctl_mqtt_mutex);
            g_ctl_mqtt = std::make_shared<AmultiosMqtt>();
            g_ctl_mqtt->subscribed = 0;
            g_ctl_mqtt->mqtt_id = "CTL/" + g_Config.sNickName + "/" + g_Config.sMACAddress;
            ctl_client = mosquitto_new(g_ctl_mqtt->mqtt_id.c_str(), true, NULL);
        }

        mosquitto_connect_callback_set(ctl_client, ctl_connect_callback);
        mosquitto_publish_callback_set(ctl_client, ctl_publish_callback);
        mosquitto_subscribe_callback_set(ctl_client, ctl_subscribe_callback);
        mosquitto_unsubscribe_callback_set(ctl_client, ctl_unsubscribe_callback);
        mosquitto_message_callback_set(ctl_client, ctl_message_callback);
        mosquitto_threaded_set(ctl_client, true);

        // initialize will message
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        mosquitto_will_set(ctl_client, "SceNetAdhocctl", sizeof(packet), &packet, 2, false);

        g_ctl_mqtt->sub_topic = g_Config.sMACAddress + "/SceNetAdhocctl";
        g_ctl_mqtt->pub_topic = "SceNetAdhocctl";

        if ((rc = mosquitto_connect(ctl_client, getModeAddress().c_str(), 1883, 60)) != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to connect, return code %s\n", g_ctl_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //rc = mosquitto_loop_start(g_ctl_mqtt->mclient);

        //if (rc == MOSQ_ERR_SUCCESS)
        //{
        //    g_ctl_mqtt->ownThread = false;
        // }

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to start loop %s\n", g_ctl_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //if (rc == MOSQ_ERR_NOT_SUPPORTED)
        //{
        WARN_LOG(AMULTIOS, "[%s] Running own thread ", g_ctl_mqtt->mqtt_id.c_str());
        g_ctl_mqtt->ownThread = true;
        ctlThread = std::thread(__AMULTIOS_CTL_START);
        //}
    }
    return rc;
}

int __AMULTIOS_CTL_SHUTDOWN()
{
    int rc = MOSQ_ERR_SUCCESS;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctlInited)
    {
        //wait disconnection!
        sleep_ms(100);
        rc = mosquitto_disconnect(ctl_client);
        //mosquitto_loop_stop(ctl_mqtt->mclient, false);

        if (ctl_mqtt->ownThread && ctlThread.joinable())
        {
            ctlThread.join();
        }

        NOTICE_LOG(AMULTIOS, "amultios_mqtt shutdown %d", rc);
        mosquitto_destroy(ctl_client);

        ctlInited = false;
    }
    NOTICE_LOG(AMULTIOS, "ctl_mqtt Thread Finished");
    return rc;
}

int __AMULTIOS_PDP_INIT()
{
    int rc = MOSQ_ERR_CONN_PENDING;

    if (!pdpInited)
    {
        pdpInited = true;
        {
            //std::lock_guard<std::mutex> lk(pdp_mqtt_mutex);
            g_pdp_mqtt.reset(new AmultiosMqtt());
            g_pdp_mqtt->subscribed = 0;
            g_pdp_mqtt->mqtt_id = "PDP/" + g_Config.sNickName + "/" + g_Config.sMACAddress;
            g_pdp_mqtt->reconnectInProgress = true;
            pdp_client = mosquitto_new(g_pdp_mqtt->mqtt_id.c_str(), true, NULL);
        }

        mosquitto_connect_callback_set(pdp_client, pdp_connect_callback);
        mosquitto_publish_callback_set(pdp_client, pdp_publish_callback);
        mosquitto_subscribe_callback_set(pdp_client, pdp_subscribe_callback);
        mosquitto_unsubscribe_callback_set(pdp_client, pdp_unsubscribe_callback);
        mosquitto_message_callback_set(pdp_client, pdp_message_callback);
        mosquitto_threaded_set(pdp_client, true);
        mosquitto_int_option(pdp_client, MOSQ_OPT_SEND_MAXIMUM, 0);
        mosquitto_int_option(pdp_client, MOSQ_OPT_RECEIVE_MAXIMUM, 0);
        mosquitto_int_option(pdp_client, MOSQ_OPT_TCP_NODELAY, true);
        if ((rc = mosquitto_connect(pdp_client, getModeAddress().c_str(), 1883, 300)) != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to connect, return code %s\n", g_pdp_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        /*rc = mosquitto_loop_start(pdp_client);

        if (rc == MOSQ_ERR_SUCCESS)
        {
            g_pdp_mqtt->ownThread = false;
        }*/

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to start loop %s\n", g_pdp_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //if (rc == MOSQ_ERR_NOT_SUPPORTED)
        //{
        WARN_LOG(AMULTIOS, "[%s] Running own thread ", g_pdp_mqtt->mqtt_id.c_str());
        g_pdp_mqtt->ownThread = true;
        pdpThread = std::thread(__AMULTIOS_PDP_START);
        //}
    }
    return rc;
}

int __AMULTIOS_PDP_SHUTDOWN()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    //auto pdp_mqtt = g_pdp_mqtt;
    if (pdpInited)
    {
        rc = mosquitto_disconnect(pdp_client);
        //mosquitto_loop_stop(pdp_mqtt->mclient, false);

        if (g_pdp_mqtt->ownThread && pdpThread.joinable())
        {
            pdpThread.join();
        }

        mosquitto_destroy(pdp_client);
        pdpInited = false;
        NOTICE_LOG(AMULTIOS, "pdp_mqtt shutdown %d", rc);
    }
    NOTICE_LOG(AMULTIOS, "pdp_mqtt Thread Finished");
    return rc;
}

int __AMULTIOS_PTP_INIT()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    if (!ptpInited)
    {
        ptpInited = true;
        {
            //std::lock_guard<std::mutex> lk(ptp_mqtt_mutex);
            g_ptp_mqtt.reset(new AmultiosMqtt());
            g_ptp_mqtt->subscribed = 0;
            g_ptp_mqtt->mqtt_id = "PTP/" + g_Config.sNickName + "/" + g_Config.sMACAddress;
            ptp_client = mosquitto_new(g_ptp_mqtt->mqtt_id.c_str(), true, NULL);
        }

        mosquitto_connect_callback_set(ptp_client, ptp_connect_callback);
        mosquitto_publish_callback_set(ptp_client, ptp_publish_callback);
        mosquitto_subscribe_callback_set(ptp_client, ptp_subscribe_callback);
        mosquitto_unsubscribe_callback_set(ptp_client, ptp_unsubscribe_callback);
        mosquitto_message_callback_set(ptp_client, ptp_message_callback);
        mosquitto_threaded_set(ptp_client, true);
        mosquitto_int_option(ptp_client, MOSQ_OPT_SEND_MAXIMUM, 0);
        mosquitto_int_option(ptp_client, MOSQ_OPT_RECEIVE_MAXIMUM, 0);
        mosquitto_int_option(ptp_client, MOSQ_OPT_TCP_NODELAY, true);
        if ((rc = mosquitto_connect(ptp_client, getModeAddress().c_str(), 1883, 60)) != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to connect, return code %s\n", g_ptp_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //rc = mosquitto_loop_start(ptp_client);
        //if (rc == MOSQ_ERR_SUCCESS)
        //{
        //    g_ptp_mqtt->ownThread = false;
        //}

        if (rc != MOSQ_ERR_SUCCESS)
        {
            ERROR_LOG(AMULTIOS, "[%s] Failed to start loop %s\n", g_ptp_mqtt->mqtt_id.c_str(), mosquitto_strerror(rc));
        }

        //if (rc == MOSQ_ERR_NOT_SUPPORTED)
        //{
        WARN_LOG(AMULTIOS, "[%s] Running own thread ", g_ptp_mqtt->mqtt_id.c_str());
        g_ptp_mqtt->ownThread = true;
        ptpThread = std::thread(__AMULTIOS_PTP_START);
        //}
    }
    return rc;
}

int __AMULTIOS_PTP_SHUTDOWN()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    //auto ptp_mqtt = g_ptp_mqtt;
    if (ptpInited)
    {
        rc = mosquitto_disconnect(ptp_client);
        //mosquitto_loop_stop(ptp_mqtt->mclient, false);

        if (g_ptp_mqtt->ownThread && ptpThread.joinable())
        {
            ptpThread.join();
        }

        mosquitto_destroy(ptp_client);

        ptpInited = false;
        NOTICE_LOG(AMULTIOS, "ptp_mqtt shutdown %d", rc);
    }
    NOTICE_LOG(AMULTIOS, "ptp_mqtt Thread Finished");
    return rc;
}

int AmultiosNetAdhocInit()
{
    int rc = MOSQ_ERR_CONN_PENDING;
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
        int rc = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
        if (rc == MOSQ_ERR_SUCCESS)
        {
            return 0;
        }
    }
    return MOSQ_ERR_CONN_PENDING;
}

int AmultiosNetAdhocctlCreate(const char *groupName)
{
    if (!trusted)
    {
        return -1;
    }

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

                if (iResult != MOSQ_ERR_SUCCESS)
                {
                    ERROR_LOG(AMULTIOS, "Mqtt Error when sending reason %s", mosquitto_strerror(iResult));
                    //threadStatus = ADHOCCTL_STATE_DISCONNECTED;
                }

                std::string room = "PARTY" + getCurrentGroup();
                if (room != "PARTY")
                {
                    std::string topic = "CHAT/PARTY" + getCurrentGroup() + "/#";
                    int rc = amultios_subscribe(topic.c_str(), 2);
                    if (rc == MOSQ_ERR_SUCCESS)
                    {
                        cmList.Add("Party Chat is now available");
                    }
                    amultios_sync();
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

            int iResult = MOSQ_ERR_CONN_PENDING;
            auto ctl_mqtt = g_ctl_mqtt;
            if (ctl_mqtt != nullptr && ctl_mqtt->connected)
            {
                iResult = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 2, 0);
            }

            if (iResult != MOSQ_ERR_SUCCESS)
            {
                ERROR_LOG(AMULTIOS, "ctl_mqtt Error when sending scan reason %d", iResult);
                //threadStatus = ADHOCCTL_STATE_DISCONNECTED;
                //if (error == ECONNABORTED || error == ECONNRESET || error == ENOTCONN) return ERROR_NET_ADHOCCTL_NOT_INITIALIZED; // A case where it need to reconnect to AdhocServer
                //return ERROR_NET_ADHOCCTL_DISCONNECTED; // ERROR_NET_ADHOCCTL_BUSY
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
        std::string room = "PARTY" + getCurrentGroup();
        if (room != "PARTY")
        {
            std::string topic = "CHAT/PARTY" + getCurrentGroup() + "/#";
            int rc = amultios_unsubscribe(topic.c_str());
            if (rc == MOSQ_ERR_SUCCESS)
            {
                cmList.Add("Party Chat is now unavailable");
            }
        }

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

        // Delete Peer Reference
        friends = NULL;
    }
    return 0;
}

int AmultiosNetAdhocctlTerm()
{
    int rc = MOSQ_ERR_CONN_PENDING;
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        AmultiosNetAdhocctlDisconnectPacketS2C packet;
        packet.base.opcode = OPCODE_AMULTIOS_LOGOUT;
        SceNetEtherAddr addres;
        getLocalMac(&addres);
        packet.mac = addres;
        rc = ctl_publish(ctl_mqtt->pub_topic.c_str(), &packet, sizeof(packet), 0, 5000L);
    }
    threadStatus = ADHOCCTL_STATE_DISCONNECTED;
    std::string room = "PARTY" + getCurrentGroup();
    if (room != "PARTY")
    {
        std::string topic = "CHAT/PARTY" + getCurrentGroup() + "/#";
        amultios_unsubscribe(topic.c_str());
        amultios_sync();
    }
    {
        std::lock_guard<std::mutex> lk(ptp_peer_mutex);
        ptp_peer_connection.clear();
    }
    return 0;
}

int AmultiosNetAdhocTerm()
{
    auto ctl_mqtt = g_ctl_mqtt;
    if (ctl_mqtt != nullptr && ctl_mqtt->connected)
    {
        ctl_unsubscribe(ctl_mqtt->sub_topic.c_str());

        if (!ctlStatusTopic.empty())
        {
            amultios_unsubscribe(ctlStatusTopic.c_str());
        }

        for (auto it : ptp_relay_topic)
        {
            if (!it.empty())
            {
                ptp_unsubscribe(it.c_str());
                it.clear();
            }
        }

        for (auto it : ptp_sub_topic)
        {
            if (!it.empty())
            {
                ptp_unsubscribe(it.c_str());
                it.clear();
            }
        }

        for (auto it : ptp_pub_topic)
        {
            it.clear();
        }

        {
            std::lock_guard<std::mutex> lk(pdp_queue_mutex);
            pdp_queue.clear();
        }

        {
            std::lock_guard<std::mutex> lk(ptp_queue_mutex);
            ptp_queue.clear();
        }

        {
            std::lock_guard<std::mutex> lk(ptp_peer_mutex);
            ptp_peer_connection.clear();
        }
    }

    return 0;
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
                std::string group_s = getCurrentGroup();
                std::string sub_topic = "/" + std::to_string(port) + "/#";
                rc = pdp_subscribe(("PDP/S/" + getMacString(saddr) + sub_topic).c_str(), g_Config.iPtpQos);
                rc = pdp_subscribe(("PDP/B/ff:ff:ff:ff:ff:ff" + sub_topic).c_str(), g_Config.iPtpQos);

                if (group_s.length() > 0)
                {
                    rc = pdp_subscribe(("PDP/" + group_s + "/ff:ff:ff:ff:ff:ff" + sub_topic).c_str(), g_Config.iPtpQos);
                }

                // Valid Socket produced
                if (rc == MOSQ_ERR_SUCCESS)
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
                            internal->id = i;
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
                        ERROR_LOG(AMULTIOS,"PDP ADHOC PORT IN USE");
                        free(internal);
                        return retval;
                    }
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
                                    std::string pdp_single_topic = "PDP/S/" + getMacString(daddr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport);
                                    const char *payload = static_cast<const char *>(data);
                                    std::string payout;
                                    size_t payoutlen = snappy::Compress(payload, len, &payout);
                                    //NOTICE_LOG(AMULTIOS, "[PDP_NETWORK] PDP send topic single %s", pdp_single_topic.c_str());

                                    if (flag)
                                    {
                                        rc = pdp_publish(pdp_single_topic, payout, (int)payoutlen, g_Config.iPtpQos, 0);
                                        if (rc == MOSQ_ERR_SUCCESS)
                                        {
                                            // auto pdp_mqtt = g_pdp_mqtt;
                                            // if (pdp_mqtt->connected)
                                            // {
                                            //     mosquitto_loop(pdp_mqtt->mclient, timeout, 1);
                                            // }
                                            return 0;
                                        }
                                        return ERROR_NET_ADHOC_WOULD_BLOCK;
                                    }

                                    rc = pdp_publish(pdp_single_topic, payout, (int)payoutlen, g_Config.iPtpQos, timeout);

                                    // auto pdp_mqtt = g_pdp_mqtt;
                                    // if (pdp_mqtt->connected)
                                    // {
                                    //     mosquitto_loop(pdp_mqtt->mclient, timeout, 1);
                                    // }

                                    if (rc == MOSQ_ERR_SUCCESS)
                                    {
                                        return 0;
                                    }

                                    if (!flag)
                                    {
                                        WARN_LOG(AMULTIOS, "Warning Send Reached Timeout timeout [%d] len[%d]", timeout, len);
                                    }
                                    return ERROR_NET_ADHOC_TIMEOUT;
                                }
                            }

                            // Broadcast Target
                            else
                            {

                                //Group Broadcast overflow the socket
                                /*SceNetEtherAddr *saddr = (SceNetEtherAddr *)socket->laddr.data;
                                int rc;
                                std::string pdp_single_topic;
                                std::string group_s = getCurrentGroup();

                                if (group_s.length() > 0)
                                {
                                    pdp_single_topic = "PDP/" + group_s + "/ff:ff:ff:ff:ff:ff/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport);
                                }
                                else
                                {
                                    pdp_single_topic = "PDP/B/ff:ff:ff:ff:ff:ff/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport);
                                }

                                if (flag)
                                {
                                    rc = pdp_publish(pdp_single_topic.c_str(), data, len, g_Config.iPtpQos, 0);
                                }
                                else
                                {
                                    rc = pdp_publish(pdp_single_topic.c_str(), data, len, g_Config.iPtpQos, timeout);
                                }*/
                                // Iterate Peers
                                peerlock.lock();

                                int rc;
                                const char *payload = static_cast<const char *>(data);
                                std::string payout;
                                size_t payoutlen = snappy::Compress(payload, len, &payout);

                                SceNetAdhocctlPeerInfo *peer = friends;
                                SceNetEtherAddr *saddr = (SceNetEtherAddr *)socket->laddr.data;
                                for (; peer != NULL; peer = peer->next)
                                {
                                    if (flag)
                                    {
                                        rc = pdp_publish("PDP/S/" + getMacString(&peer->mac_addr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport), payout, (int)payoutlen, g_Config.iPtpQos, 0);
                                    }
                                    else
                                    {
                                        rc = pdp_publish("PDP/S/" + getMacString(&peer->mac_addr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(socket->lport), payout, (int)payoutlen, g_Config.iPtpQos, timeout);
                                    }
                                }
                                // Free Peer Lock
                                peerlock.unlock();

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

                //if (pdp_queue.size() > 0)
                //{
                //INFO_LOG(AMULTIOS, "Start PDP Loop");
                std::vector<PDPMessage>::iterator it = std::find_if(pdp_queue.begin(), pdp_queue.end(), [&socket](PDPMessage const &obj) {
                    //INFO_LOG(AMULTIOS,"INSIDE QUEUE[%d] [%s]:[%d] data len [%d]",(int) pdp_queue.size(),getMacString(&obj.destinationMac).c_str(),obj.dport,obj.payloadlen);
                    return /*isSameMAC(&obj.destinationMac, &socket->laddr) && macInNetwork(&obj.sourceMac) && */ obj.dport == socket->lport;
                });

                // auto pdp_mqtt = g_pdp_mqtt;
                // if (pdp_mqtt->connected)
                // {
                //     mosquitto_loop(pdp_mqtt->mclient, timeout, 1);
                // }

                if (it != pdp_queue.end())
                {
                    std::lock_guard<std::mutex> lk(pdp_queue_mutex);
                    if (macInNetwork(&it->sourceMac))
                    {
                        memcpy(buf, it->payload.data(), it->payloadlen);
                        *saddr = it->sourceMac;
                        *sport = (uint16_t)it->sport;
                        *len = it->payloadlen;
                        DEBUG_LOG(AMULTIOS, "Got Pdp Message [%d]", it->payloadlen);
                        pdp_queue.erase(it);
                        return 0;
                    }
                    DEBUG_LOG(AMULTIOS, "Discarding Uknown Message");
                    pdp_queue.erase(it);
                }

                DEBUG_LOG(AMULTIOS, "No Message Found");
                if (flag)
                    return ERROR_NET_ADHOC_WOULD_BLOCK;

                if (!flag)
                {
                    DEBUG_LOG(AMULTIOS, "Warning Receive Reached Timeout timeout [%d] len[%d]", timeout, *len);
                }
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
                
                if (!pdp_topic.at(id - 1).empty())
                {
                    int rc = pdp_unsubscribe(("PDP/S/" + getMacString(&sock->laddr) + pdp_topic.at(id - 1)).c_str());
                    rc = pdp_unsubscribe(("PDP/B/ff:ff:ff:ff:ff:ff" + pdp_topic.at(id - 1)).c_str());

                    std::string group_s = getCurrentGroup();
                    if (group_s.length() > 0)
                    {
                        rc = pdp_unsubscribe(("PDP/" + group_s + "/ff:ff:ff:ff:ff:ff" + pdp_topic.at(id - 1)).c_str());
                    }
                }

                //remove that packet!
                {
                    std::lock_guard<std::mutex> lock(pdp_queue_mutex);
                    pdp_queue.erase(
                        std::remove_if(pdp_queue.begin(), pdp_queue.end(),
                                       [sock](const PDPMessage &o) { return o.dport == sock->lport; }),
                        pdp_queue.end());
                }
                

                pdp[id - 1] = NULL;
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

                            std::string sub_topic = "PTP/D/" + getMacString(saddr) + "/" + std::to_string(sport) + "/" + getMacString(daddr) + "/" + std::to_string(dport);
                            std::string pub_topic = "PTP/D/" + getMacString(daddr) + "/" + std::to_string(dport) + "/" + getMacString(saddr) + "/" + std::to_string(sport);
                            std::string open_topic = "PTP/A/" + getMacString(saddr) + "/" + std::to_string(sport) + "/" + getMacString(daddr) + "/" + std::to_string(dport);
                            int rc = ptp_subscribe(open_topic.c_str(), 2);

                            //data flag which better 1 or 2?
                            rc = ptp_subscribe(sub_topic.c_str(), g_Config.iPtpQos);
                            SceNetAdhocPtpStat *internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));

                            // Allocated Memory
                            if (internal != NULL && rc == MOSQ_ERR_SUCCESS)
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

                        ERROR_LOG(SCENET, "OPEN BIND FAILED port %d", sport);
                        closesocket(tcpsocket);
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

                    if (found /*&& macInNetwork(&it->sourceMac)*/)
                    {

                        std::string sub_topic = "PTP/D/" + getMacString(&it->destinationMac) + "/" + std::to_string(it->dport) + "/" + getMacString(&it->sourceMac) + "/" + std::to_string(it->sport);
                        std::string accept_topic = "PTP/A/" + getMacString(&it->sourceMac) + "/" + std::to_string(it->sport) + "/" + getMacString(&it->destinationMac) + "/" + std::to_string(it->dport);
                        // Allocate Memory

                        // data flag which better 1 or 2?
                        int rc = ptp_subscribe(sub_topic.c_str(), g_Config.iPtpQos);

                        //uint8_t send = PTP_AMULTIOS_ACCEPT;
                        std::string send(1, PTP_AMULTIOS_ACCEPT);
                        rc = ptp_publish(accept_topic, send, (int)send.length(), 2, 0);

                        int iResult = 0;
                        if (rc == MOSQ_ERR_SUCCESS)
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
                                        it->id = i + 1;
                                        it->states = PTP_AMULTIOS_ESTABLISHED;
                                    }
                                    ptp_relay_topic.at(i) = accept_topic;
                                    ptp_sub_topic.at(i) = sub_topic;
                                    ptp_pub_topic.at(i) = "PTP/D/" + getMacString(&it->sourceMac) + "/" + std::to_string(it->sport) + "/" + getMacString(&it->destinationMac) + "/" + std::to_string(it->dport);
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
                    std::string connect_topic = "PTP/C/" + getMacString(&socket->paddr) + "/" + std::to_string(socket->pport) + "/" + getMacString(&socket->laddr) + "/" + std::to_string(socket->lport);

                    // Connect Socket to Peer (Nonblocking)
                    //uint8_t send = PTP_AMULTIOS_CONNECT;
                    std::string send(1, PTP_AMULTIOS_CONNECT);
                    int rc = ptp_publish(connect_topic, send, (int)send.length(), 2, 0);

                    bool found = false;
                    std::vector<PTPConnection>::iterator it;
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
                    else
                    {
                        // Nonblocking Mode
                        if (flag)
                        {
                            return ERROR_NET_ADHOC_WOULD_BLOCK;
                        }
                        else
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

                            if (found)
                            {
                                // Set Connected State
                                {
                                    std::lock_guard<std::mutex> lock(ptp_peer_mutex);
                                    it->id = id;
                                    it->states = PTP_AMULTIOS_ESTABLISHED;
                                }
                                socket->state = ADHOC_PTP_STATE_ESTABLISHED;
                                NOTICE_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Already Connected", id, socket->lport);
                                // Success
                                return 0;
                            }

                            return ERROR_NET_ADHOC_TIMEOUT;
                        }
                    }
                }
                NOTICE_LOG(SCENET,"CONNECT RETURN REFUSED");
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

            {
                std::lock_guard<std::mutex> lk(ptp_peer_mutex);
                ptp_peer_connection.erase(
                    std::remove_if(ptp_peer_connection.begin(), ptp_peer_connection.end(),
                                   [id](const PTPConnection &o) { return o.id == id; }),
                    ptp_peer_connection.end());
            }

            {
                std::lock_guard<std::mutex> lk(ptp_queue_mutex);
                ptp_queue.erase(
                    std::remove_if(ptp_queue.begin(), ptp_queue.end(),
                                   [socket](const PTPMessage &it) {
                                       return (isSameMAC(&it.destinationMac, &socket->laddr) && it.dport == socket->lport && isSameMAC(&it.sourceMac, &socket->paddr) && it.sport == socket->pport);
                                   }),
                    ptp_queue.end());
            }

            if (!ptp_pub_topic.at(id - 1).empty())
            {
                ptp_pub_topic.at(id - 1).clear();
            }

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
            closesocket(socket->id);
            free(socket);
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
                        int bindPort = sport + bindOffset;
                        sockaddr_in addr;
                        addr.sin_family = AF_INET;
                        addr.sin_addr.s_addr = INADDR_ANY;
                        addr.sin_port = htons(bindPort);

                        int iResult = 0;
                        // Bound Socket to local Port
                        if ((iResult = bind(tcpsocket, (sockaddr *)&addr, sizeof(addr))) == 0)
                        {
                            // Switch into Listening Mode
                            std::string sub_topic = "PTP/C/" + getMacString(saddr) + "/" + std::to_string(sport) + "/#";
                            int rc = ptp_subscribe(sub_topic.c_str(), 2);
                            if ((iResult = listen(tcpsocket, backlog)) == 0 && rc == MOSQ_ERR_SUCCESS)
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
                                        internal->id = tcpsocket;

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

                    std::string payout;
                    size_t payoutlen = snappy::Compress(data, *len, &payout);
                    // Schedule Timeout Removal
                    if (flag)
                        timeout = 0;

                    int rc = ptp_publish(ptp_pub_topic.at(id - 1), payout, (int)payoutlen, g_Config.iPtpQos, timeout);

                    // Success
                    if (rc == MOSQ_ERR_SUCCESS)
                    {
                        // Save Length
                        INFO_LOG(AMULTIOS,"[%s] send message len [%d]",ptp_pub_topic.at(id-1).c_str(),*len);
                        *len = *len;
                        // Return Success
                        return 0;
                    }

                    if (ptpInited)
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
                    if (ptp_queue.size() > 0)
                    {
                        std::vector<PTPMessage>::iterator find = std::find_if(ptp_queue.begin(), ptp_queue.end(), [&socket](PTPMessage const &it) {
                            return (isSameMAC(&it.destinationMac, &socket->laddr) && it.dport == socket->lport && isSameMAC(&it.sourceMac, &socket->paddr) && it.sport == socket->pport);
                        });

                        if (find != ptp_queue.end())
                        {
                            std::lock_guard<std::mutex> lk(ptp_queue_mutex);
                            memcpy(buf, find->payload.data(), find->payloadlen);
                            *len = find->payloadlen;
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

                        //if (ptp_queue.size() > 0)
                        //{
                        std::vector<PTPMessage>::iterator find = std::find_if(ptp_queue.begin(), ptp_queue.end(), [&socket](PTPMessage const &it) {
                            return (isSameMAC(&it.destinationMac, &socket->laddr) && it.dport == socket->lport && isSameMAC(&it.sourceMac, &socket->paddr) && it.sport == socket->pport);
                        });

                        if (find != ptp_queue.end())
                        {
                            std::lock_guard<std::mutex> lk(ptp_queue_mutex);
                            memcpy(buf, find->payload.data(), find->payloadlen);
                            *len = find->payloadlen;
                            ptp_queue.erase(find);
                            return 0;
                        }
                        //}
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

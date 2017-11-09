#include "proAdhoc.h"

#define SERVER_NAME_LENGTH 10
#define CHAT_GROUPNAME_LENGTH 18

// Login Amutios Lobby and Game Lobby
#define OPCODE_AMULTIOS_CHAT_LOGIN 13
// Login Group Lobby
#define OPCODE_AMULTIOS_CHAT_CONNECT_GAME 14
#define OPCODE_AMULTIOS_CHAT_CONNECT_GROUP 15
#define OPCODE_AMULTIOS_CHAT_DISCONNECT_GROUP 16
#define OPCODE_GAME_CHAT 17

//server name + group name to make virtual room

typedef struct ChatGroupName {
	uint8_t data[CHAT_GROUPNAME_LENGTH];
} PACK ChatGroupName;

// C2S Chat Login Packet
typedef struct {
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlNickname name;
	char server[SERVER_NAME_LENGTH];
	char pin[PIN_LENGTH];
	uint8_t enableGlobal;
	uint8_t enableGame;
} PACK ChatLoginPacketC2S;

// Connect Game Lobby Packet
typedef struct {
	SceNetAdhocctlPacketBase base;
	SceNetAdhocctlProductCode game;
}PACK ChatConnectGamePacketC2S;

// C2S Connect Group Packet
typedef struct {
	SceNetAdhocctlPacketBase base;
	ChatGroupName group;
} PACK ChatConnectPacketC2S;

// C2S Disconnect Group & Game Packet
typedef struct {
	SceNetAdhocctlPacketBase base;
} PACK ChatDisconnectPacketC2S;


// CLIENT HEADER

#define CHAT_CLIENT_WAITING 0
#define CHAT_CLIENT_CONNECTED 1
#define CHAT_CLIENT_DISCONNECTED 2

#define CHAT_GUI_GLOBAL 0
#define CHAT_GUI_GROUP 1
#define CHAT_GUI_GAME 2
#define CHAT_GUI_ALL 3

extern int chatsocket;
extern std::thread ChatClientThread;
extern bool ChatClientRunning;
extern int chatclientstatus;
void sendChat(std::string chatString);
void connectChatGame(SceNetAdhocctlAdhocId *adhoc_id);
void connectChatGroup(const char * groupname);
void disconnectChatGroup();
std::vector<std::string> getChatLog();
extern bool chatScreenVisible;
extern bool updateChatScreen;
extern int newChat;
extern int chatGuiStatus;
void InitChat();
void TerminateChat();
extern std::string createVirtualGroup(const char * groupname);
extern void getServerName(char * servername);
int ChatClient(int port);
void Reconnect();

extern std::vector<std::string> GroupChatLog;
extern std::vector<std::string> AllChatLog;

std::vector<std::string> Split(const std::string& str);
bool isPlayer(std::string& pname, std::string& logname);
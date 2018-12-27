#include "proAdhoc.h"

#define SERVER_NAME_LENGTH 10
#define CHAT_GROUPNAME_LENGTH 18

// Login Amutios Lobby and Game Lobby
#define OPCODE_AMULTIOS_CHAT_LOGIN 10
// Login Group Lobby
#define OPCODE_AMULTIOS_CHAT_CONNECT_GAME 14
#define OPCODE_AMULTIOS_CHAT_CONNECT_GROUP 15
#define OPCODE_AMULTIOS_CHAT_DISCONNECT_GROUP 16
#define OPCODE_GAME_CHAT 17


// CLIENT HEADER
#define CHAT_CLIENT_WAITING 0
#define CHAT_CLIENT_CONNECTED 1
#define CHAT_CLIENT_DISCONNECTED 2

#define CHAT_GUI_ALL 0
#define CHAT_GUI_GROUP 1
#define CHAT_GUI_GAME 2
#define CHAT_GUI_SERVER 3

#define CHAT_ADD_ALL 0
#define CHAT_ADD_GROUP 1
#define CHAT_ADD_GAME 2
#define CHAT_ADD_SERVER 3
#define CHAT_ADD_ALLGROUP 4
//server name + group name to make virtual room

class ChatMessages {
public:
	struct ChatMessage {
		std::string name;
		std::string room;
		std::string text;
		uint32_t namecolor;
		uint32_t roomcolor;
		uint32_t textcolor;
		bool onlytext;
	};
	
	bool isChatScreenVisible();

	void Update(int chatAdd);
	float getLastUpdate();

	void Lock() {
		chatmutex_.lock();
	}

	void Unlock() {
		chatmutex_.unlock();
	}

	void doOSMUpdate();
	bool getOSMUpdate() { return updateOSMFlag; }
	void doChatUpdate();
	bool getChatUpdate() { return updateChatFlag; }
	void toogleChatScreen(bool flag);

	void Add(const std::string &text, const std::string &name,int room = 0, uint32_t namecolor = 0xF39621);
	const std::list<ChatMessage> &Messages(int chatGuiStatus) {
		if (chatGuiStatus == CHAT_GUI_GROUP) {
			if (GroupChatDb.size() > 50) {
				std::list<ChatMessage>::iterator itEnd;
				itEnd = GroupChatDb.begin();
				advance(itEnd, 39);
				GroupChatDb.erase(GroupChatDb.begin(), itEnd);
			}
			return GroupChatDb;
		}
		if (AllChatDb.size() > 50) {
			std::list<ChatMessage>::iterator itEnd;
			itEnd = AllChatDb.begin();
			advance(itEnd, 39);
			AllChatDb.erase(AllChatDb.begin(),itEnd);
		}
		return AllChatDb;
	}
private:
	std::list<ChatMessage> GroupChatDb;
	std::list<ChatMessage> AllChatDb;
	std::mutex chatmutex_;
	std::mutex chatScreenMutex_;
	std::mutex chatUpdateMutex_;
	float lastUpdate;
	bool chatScreenVisible;
	bool updateOSMFlag;
	bool updateChatFlag;
};

#ifdef _MSC_VER 
#pragma pack(push, 1)
#endif

typedef struct ChatGroupName {
	uint8_t data[CHAT_GROUPNAME_LENGTH];
} PACK ChatGroupName;

// C2S Chat Login Packet
typedef struct {
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlNickname name;
	char pin[PIN_LENGTH];
	char revision[20];
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

#ifdef _MSC_VER 
#pragma pack(pop)
#endif

extern int chatsocket;
extern std::thread ChatClientThread;
extern bool ChatClientRunning;
extern int chatclientstatus;
void sendChat(std::string chatString);
void connectChatGame(SceNetAdhocctlAdhocId *adhoc_id);
void connectChatGroup(const char * groupname);
void disconnectChatGroup();
extern int chatGuiStatus;
void InitChat();
void TerminateChat();
extern std::string createVirtualGroup(const char * groupname);
extern void getServerName(char * servername);
int ChatClient(int port);
void Reconnect();
extern ChatMessages cmList;
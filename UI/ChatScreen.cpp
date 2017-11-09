#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "ChatScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/AmultiosChatClient.h"
#include "i18n/i18n.h"
#include <ctype.h>
#include "util/text/utf8.h"

std::string chatTo = "All";
int chatGuiIndex = 0;

ChatScreen::ChatScreen() {
	alpha_ = 0.0f;
}

void ChatScreen::CreateViews() {
	using namespace UI;

	I18NCategory *n = GetI18NCategory("Networking");
	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	float yres = screenManager()->getUIContext()->GetBounds().h;

	switch (g_Config.iChatScreenPosition) {
	// the chat screen size is still static 280,250 need a dynamic size based on device resolution 
	case 0:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight() , 290, NONE, NONE, 250, true));
		break;
	case 1:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), dc.GetBounds().centerX(), NONE, NONE, 250, true));
		break;
	case 2:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), NONE, NONE, 290, 250, true));
		break;
	case 3:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), 290, 250, NONE, NONE, true));
		break;
	case 4:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), dc.GetBounds().centerX(), 250, NONE, NONE, true));
		break;
	case 5:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), NONE, 250, 290, NONE, true));
		break;
	}

	root_->Add(box_);
	box_->SetBG(UI::Drawable(0x00303030));
	box_->SetHasDropShadow(false);

	UI::ChoiceDynamicValue *channel = new ChoiceDynamicValue(&chatTo,new LinearLayoutParams(110,50));
	channel->OnClick.Handle(this, &ChatScreen::OnChangeChannel);

	scroll_ = box_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	scroll_->setBobColor(0x00FFFFFF);
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);

	LinearLayout *bottom = box_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(ChatScreenWidth(), WRAP_CONTENT)));
	bottom->Add(channel);
	
#if defined(_WIN32) || defined(USING_QT_UI)
	chatEdit_ = bottom->Add(new ChatTextEdit("", n->T("Chat Here"), new LinearLayoutParams((ChatScreenWidth() -120),50)));
	chatEdit_->SetMaxLen(63);
	chatEdit_->OnEnter.Handle(this, &ChatScreen::OnSubmit);
#if defined(USING_WIN_UI)
	//freeze  the ui when using ctrl + C hotkey need workaround
	if (g_Config.bBypassOSKWithKeyboard && !g_Config.bFullScreen)
	{
		std::wstring titleText = ConvertUTF8ToWString(n->T("Chat"));
		std::wstring defaultText = ConvertUTF8ToWString(n->T("Chat Here"));
		std::wstring inputChars;
		if (System_InputBoxGetWString(titleText.c_str(), defaultText, inputChars)) {
			//chatEdit_->SetText(ConvertWStringToUTF8(inputChars));
			sendChat(ConvertWStringToUTF8(inputChars));
		}
	}
#endif
#elif defined(__ANDROID__)
	bottom->Add(new Button(n->T("Chat Here"), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->OnClick.Handle(this, &ChatScreen::OnSubmit);
#endif
	if (g_Config.bEnableQuickChat) {
		LinearLayout *quickChat = box_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		quickChat->Add(new Button(n->T("1"), new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatScreen::OnQuickChat1);
		quickChat->Add(new Button(n->T("2"), new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatScreen::OnQuickChat2);
		quickChat->Add(new Button(n->T("3"), new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatScreen::OnQuickChat3);
		quickChat->Add(new Button(n->T("4"), new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatScreen::OnQuickChat4);
		quickChat->Add(new Button(n->T("5"), new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatScreen::OnQuickChat5);
	}
	//CreatePopupContents(box_);
#if defined(_WIN32) || defined(USING_QT_UI)
	UI::EnableFocusMovement(true);
	root_->SetDefaultFocusView(chatEdit_);
	root_->SetFocus();
#else
	//root_->SetDefaultFocusView(box_);
	//box_->SubviewFocused(scroll_);
	//root_->SetFocus();
#endif
	chatScreenVisible = true;
	newChat = 0;
	UpdateChat();
}

void ChatScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
}

UI::EventReturn ChatScreen::OnSubmit(UI::EventParams &e) {
#if defined(_WIN32) || defined(USING_QT_UI)
	std::string chat = chatEdit_->GetText();
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	sendChat(chat);
#elif defined(__ANDROID__)
	System_SendMessage("inputbox", "Chat:");
#endif
	return UI::EVENT_DONE;
}


UI::EventReturn ChatScreen::OnQuickChat1(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat0);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatScreen::OnQuickChat2(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat1);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatScreen::OnQuickChat3(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat2);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatScreen::OnQuickChat4(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat3);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatScreen::OnQuickChat5(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat4);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatScreen::OnChangeChannel(UI::EventParams &params) {

	chatGuiIndex += 1;
	if (chatGuiIndex >= 4) {
		chatGuiIndex = 0;
	}

	switch (chatGuiIndex)
	{
	case 0:
		chatTo = "All";
		chatGuiStatus = CHAT_GUI_ALL;
		break;
	case 1:
		chatTo = "Server";
		chatGuiStatus = CHAT_GUI_GLOBAL;
		break;
	case 2:
		chatTo = "Group";
		chatGuiStatus = CHAT_GUI_GROUP;
		break;
	//case 3:
		//chatTo = "Game";
		//chatGuiStatus = CHAT_GUI_GAME;
		//break;
	default:
		chatTo = "All";
		chatGuiStatus = CHAT_GUI_ALL;
		break;
	}
	UpdateChat();
	return UI::EVENT_DONE;
}

/*
	maximum chat length in one message from server is only 64 character
	need to split the chat to fit the static chat screen size
	if the chat screen size become dynamic from device resolution
	we need to change split function logic also.
*/

void ChatScreen::UpdateChat() {
	using namespace UI;
	
	if (chatVert_ != NULL) {
		chatVert_->Clear(); 
		std::vector<std::string> chatLog = getChatLog();
		for (auto i : chatLog) {
			//split long text
			uint32_t namecolor = 0xF6B629;
			uint32_t textcolor = 0xFFFFFF;
			uint32_t infocolor = 0x35D8FD;

			std::string name = g_Config.sNickName.c_str();
			std::string displayname = i.substr(0, i.find(':'));
			std::string chattext = i.substr(displayname.length());
			
			if (isPlayer(name.substr(0,8),displayname)) {
				namecolor = 0x3539E5;
			}

			if (i[displayname.length()] != ':') {
				TextView *v = chatVert_->Add(new TextView(i, FLAG_DYNAMIC_ASCII, true));
				v->SetTextColor(0xFF000000 | infocolor);
			}
			else {
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
				if (chatGuiStatus == CHAT_GUI_ALL && displayname.substr(0, 8) == "[Group] ") {
					TextView *GroupView = line->Add(new TextView(displayname.substr(0,7), FLAG_DYNAMIC_ASCII, true));
					GroupView->SetTextColor(0xFF000000 | infocolor);
					TextView *nameView = line->Add(new TextView(displayname.substr(8,displayname.length()), FLAG_DYNAMIC_ASCII, true));
					nameView->SetTextColor(0xFF000000 | namecolor);
				}
				else {
					TextView *nameView = line->Add(new TextView(displayname, FLAG_DYNAMIC_ASCII, true));
					nameView->SetTextColor(0xFF000000 | namecolor);
				}

				if (chattext.length() > 45) {
					std::vector<std::string> splitted = Split(chattext);
					std::string one = splitted[0];
					std::string two = splitted[1];
					TextView *oneview = line->Add(new TextView(one, FLAG_DYNAMIC_ASCII, true));
					oneview->SetTextColor(0xFF000000 | textcolor);
					TextView *twoview = chatVert_->Add(new TextView(two, FLAG_DYNAMIC_ASCII, true));
					twoview->SetTextColor(0xFF000000 | textcolor);
				}
				else {
					TextView *chatView = line->Add(new TextView(chattext, FLAG_DYNAMIC_ASCII, true));
					chatView->SetTextColor(0xFF000000 | textcolor);
				}
			}
		}
		toBottom_ = true;
	}
	
}

bool ChatScreen::touch(const TouchInput &touch) {

	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		return UIDialogScreen::touch(touch);
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y)){
		screenManager()->finishDialog(this, DR_BACK);
	}

	return UIDialogScreen::touch(touch);
}

void ChatScreen::update() {
	UIDialogScreen::update();

	alpha_ = 1.0f;

	
	if (scroll_ && toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}

	if (updateChatScreen) {
		UpdateChat();
		updateChatScreen = false;
	}
}


ChatScreen::~ChatScreen() {
	chatScreenVisible = false;
}

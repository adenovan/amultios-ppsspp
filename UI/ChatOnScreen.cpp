#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/AmultiosChatClient.h"
#include "i18n/i18n.h"
#include "ChatOnScreen.h"

ChatOnScreen::ChatOnScreen() {

};

ChatOnScreen::~ChatOnScreen() {

};

void ChatOnScreen::CreateViews() {
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
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatOnScreenWidth(), ChatOnScreenHeight(), 290, NONE, NONE, 250, true));
		break;
	case 1:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatOnScreenWidth(), ChatOnScreenHeight(), dc.GetBounds().centerX(), NONE, NONE, 250, true));
		break;
	case 2:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatOnScreenWidth(), ChatOnScreenHeight(), NONE, NONE, 290, 250, true));
		break;
	case 3:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatOnScreenWidth(), ChatOnScreenHeight(), 290, 250, NONE, NONE, true));
		break;
	case 4:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatOnScreenWidth(), ChatOnScreenHeight(), dc.GetBounds().centerX(), 250, NONE, NONE, true));
		break;
	case 5:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatOnScreenWidth(), ChatOnScreenHeight(), NONE, 250, 290, NONE, true));
		break;
	}

	root_->Add(box_);
	box_->SetBG(UI::Drawable(0x00303030));
	box_->SetHasDropShadow(false);
};

void ChatOnScreen::update() {
	if (chatScreenVisible) {
		alpha_ = 0.f;
	}
	else {

	}
};

void ChatOnScreen::UpdateChat() {
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

			if (isPlayer(name.substr(0, 8), displayname)) {
				namecolor = 0x3539E5;
			}

			if (i[displayname.length()] != ':') {
				TextView *v = chatVert_->Add(new TextView(i, FLAG_DYNAMIC_ASCII, true));
				v->SetTextColor(0xFF000000 | infocolor);
			}
			else {
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
				if (chatGuiStatus == CHAT_GUI_ALL && displayname.substr(0, 8) == "[Group] ") {
					TextView *GroupView = line->Add(new TextView(displayname.substr(0, 7), FLAG_DYNAMIC_ASCII, true));
					GroupView->SetTextColor(0xFF000000 | infocolor);
					TextView *nameView = line->Add(new TextView(displayname.substr(8, displayname.length()), FLAG_DYNAMIC_ASCII, true));
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
	}
};
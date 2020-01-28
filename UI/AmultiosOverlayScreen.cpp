#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "AmultiosOverlayScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/amultios.h"
#include "i18n/i18n.h"
#include <ctype.h>
#include "util/text/utf8.h"
#include "base/timeutil.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameInfoCache.h"
#include "json/json_reader.h"

void AmultiosOverlayScreen::CreateViews()
{
	using namespace UI;

	I18NCategory *aa = GetI18NCategory("Amultios");
	UIContext &dc = *screenManager()->getUIContext();
	Margins actionMenuMargins(0, 0, 15, 0);
	Margins lineMargins(10, 10, 10, 10);
	tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	ViewGroup *leftColumn = tabHolder_;

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	if (g_Config.bPauseMenuExitsEmulator)
	{
		I18NCategory *mm = GetI18NCategory("MainMenu");
		rightColumnItems->Add(new Choice(aa->T("Exit")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnExitToMenu);
	}
	else
	{
		rightColumnItems->Add(new Choice(aa->T("Exit to menu")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnExitToMenu);
	}

	rightColumnItems->Add(new Spacer(25.0));
	std::string gameId = g_paramSFO.GetDiscID();
	if (g_Config.hasGameConfig(gameId))
	{
		rightColumnItems->Add(new Choice(aa->T("Game Settings")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(aa->T("Delete Game Config")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnDeleteConfig);
	}
	else
	{
		rightColumnItems->Add(new Choice(aa->T("Settings")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(aa->T("Create Game Config")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnCreateConfig);
	}

	tabHolder_->SetTag("AmultiosHome");
	//tabHolder_->SetClip(true);

	LinearLayout *box_ = new LinearLayout(ORIENT_VERTICAL);
	scrollChat_ = box_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(0.8f)));
	scrollChat_->SetTag("AmultiosChatScreen");
	chatVert_ = scrollChat_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);

	ScrollView *scrollStatus = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, lineMargins));
	scrollStatus->SetTag("AmultiosPlayerStatus");
	statusVert_ = scrollStatus->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	statusVert_->SetSpacing(0);

	ScrollView *scrollAccount = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollStatus->SetTag("AmultiosAccountInformation");

	rightColumnItems->Add(new Spacer(50.0));
	Choice *continueChoice = rightColumnItems->Add(new Choice(aa->T("Continue")));
	continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

#if !defined(MOBILE_DEVICE)
	chatEdit_ = box_->Add(new TextEdit("", aa->T("Type Something"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatEdit_->SetMaxLen(144);
	chatEdit_->OnEnter.Handle(this, &AmultiosOverlayScreen::OnSubmit);
	UI::EnableFocusMovement(true);
#else
	box_->Add(new Spacer(20.0));
	rightColumnItems->Add(new Choice(aa->T("Type Something")))->OnClick.Handle(this, &AmultiosOverlayScreen::OnSubmit);
#endif

	tabHolder_->AddTab(aa->T("Amultios"), box_);
	tabHolder_->AddTab(aa->T("Player Status"), scrollStatus);
	tabHolder_->AddTab(aa->T("Account Information"), scrollAccount);
	tabHolder_->SetCurrentTab(0, true);
	root_ = new LinearLayout(ORIENT_HORIZONTAL);

#if !defined(MOBILE_DEVICE)
	rightColumn->ReplaceLayoutParams(new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
#else
	rightColumn->ReplaceLayoutParams(new LinearLayoutParams(250, FILL_PARENT, actionMenuMargins));
#endif
	root_->Add(leftColumn);
	root_->Add(rightColumn);
	root_->SetBG(UI::Drawable(0x88000000));

#if !defined(MOBILE_DEVICE)
	root_->SetDefaultFocusView(chatEdit_);
	root_->SetFocus();
#endif
	cmList.listenPlayerStatus();
	cmList.Update();
}

void AmultiosOverlayScreen::dialogFinished(const Screen *dialog, DialogResult result)
{
	UpdateUIState(UISTATE_INGAME);
}

UI::EventReturn AmultiosOverlayScreen::OnSubmit(UI::EventParams &e)
{
#if defined(__ANDROID__)
	System_SendMessage("inputbox", "Chat:");
#else
	std::string chat = chatEdit_->GetText();
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	cmList.ParseCommand(chat);
#endif
	return UI::EVENT_DONE;
}

// UI::EventReturn AmultiosOverlayScreen::OnChangeChannel(UI::EventParams &params) {

// 	chatGuiIndex += 1;
// 	if (chatGuiIndex >= 2) {
// 		chatGuiIndex = 0;
// 	}

// 	switch (chatGuiIndex)
// 	{
// 	case 0:
// 		chatTo = "All";
// 		chatGuiStatus = CHAT_GUI_ALL;
// 		break;
// 	case 1:
// 		chatTo = "Group";
// 		chatGuiStatus = CHAT_GUI_GROUP;
// 		break;
// 	//case 2:
// 		//chatTo = "Server";
// 		//chatGuiStatus = CHAT_GUI_SERVER;
// 		//break;
// 	//case 3:
// 		//chatTo = "Game";
// 		//chatGuiStatus = CHAT_GUI_GAME;
// 		//break;
// 	default:
// 		chatTo = "All";
// 		chatGuiStatus = CHAT_GUI_ALL;
// 		break;
// 	}
// 	UpdateChat();
// 	return UI::EVENT_DONE;
// }

/*
	maximum chat length in one message from server is only 64 character
	need to split the chat to fit the static chat screen size
	if the chat screen size become dynamic from device resolution
	we need to change split function logic also.
*/

void AmultiosOverlayScreen::UpdateChat()
{
	using namespace UI;

	if (chatVert_ != nullptr)
	{
		chatVert_->Clear();
		Margins lineMargins(0, 0, 0, 5);
		std::list<ChatMessages::ChatMessage> messages = cmList.GetMessages();

		int maxLength = 90;
#if !defined(MOBILE_DEVICE)
		maxLength = 90;
#else
		maxLength = 60;
#endif
		int currentTextLength = 0;
		for (auto iter = messages.begin(); iter != messages.end(); ++iter)
		{
			currentTextLength = iter->text.length() + iter->room.length() + iter->name.length();
			if (currentTextLength > maxLength)
			{
				int start = 0;
				int pos = maxLength - (iter->room.length() + iter->name.length());
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT,lineMargins)));
				TextView *GroupView = line->Add(new TextView(" [" + iter->room + "]", FLAG_DYNAMIC_ASCII, true));
				GroupView->SetTextColor(0xFF000000 | iter->roomcolor);
				TextView *nameView = line->Add(new TextView(iter->name, FLAG_DYNAMIC_ASCII, true));
				nameView->SetTextColor(0xFF000000 | iter->namecolor);

				std::string remain = iter->text;
				if (pos > 0)
				{
					auto it = std::find(iter->text.begin() + pos, iter->text.end(), ' ');
					if (it != iter->text.end())
					{
						pos = std::distance(iter->text.begin(), it);
						pos += 1;
					}

					TextView *chatView = line->Add(new TextView(iter->text.substr(start, pos), FLAG_DYNAMIC_ASCII, true));
					chatView->SetTextColor(0xFF000000 | iter->textcolor);
					remain = iter->text.substr(pos, iter->text.length());
				}

				while (remain.length() > maxLength)
				{
					std::string part = remain.substr(0, maxLength);
					LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT,lineMargins)));
					TextView *chatView = line->Add(new TextView(part, FLAG_DYNAMIC_ASCII, true));
					chatView->SetTextColor(0xFF000000 | iter->textcolor);
					remain = remain.substr(part.length(), remain.length());
				}

				LinearLayout *linelast = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT,lineMargins)));
				TextView *chatViewlast = linelast->Add(new TextView(remain, FLAG_DYNAMIC_ASCII, true));
				chatViewlast->SetTextColor(0xFF000000 | iter->textcolor);
			}
			else
			{
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT,lineMargins)));
				TextView *GroupView = line->Add(new TextView(" [" + iter->room + "]", FLAG_DYNAMIC_ASCII, true));
				GroupView->SetTextColor(0xFF000000 | iter->roomcolor);

				TextView *nameView = line->Add(new TextView(iter->name, FLAG_DYNAMIC_ASCII, true));
				nameView->SetTextColor(0xFF000000 | iter->namecolor);
				TextView *chatView = line->Add(new TextView(iter->text, FLAG_DYNAMIC_ASCII, true));
				chatView->SetTextColor(0xFF000000 | iter->textcolor);
			}
		}
		toBottom_ = true;
	}
}

void AmultiosOverlayScreen::UpdateStatus()
{
	using namespace UI;
	json::JsonReader reader(cmList.getPlayerStatus().c_str(), cmList.getPlayerStatus().size());
	const json::JsonGet root = reader.root();
	if (!root)
	{
		ERROR_LOG(AMULTIOS, "Failed to parse json");
		return;
	}

	if (statusVert_ == nullptr)
	{
		return;
	}
	statusVert_->Clear();
	Margins lineMargins(0, 0, 0, 0);

	const JsonNode *gameNode = root.getArray("status");

	for (const JsonNode *pgame : gameNode->value)
	{
		json::JsonGet game = pgame->value;
		std::string GameName = game.getString("GameName", "Game");
		std::string GamePlayer = " (" + std::to_string(game.getInt("GamePlayer")) + ")";
		GameName.append(GamePlayer);

		LinearLayout *header = statusVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		header->Add(new ItemHeader(GameName));

		const JsonNode *groupNode = game.getArray("Group");
		for (const JsonNode *pgroup : groupNode->value)
		{
			json::JsonGet group = pgroup->value;
			std::string GroupName = group.getString("GroupName", "Group");
			std::string Players = group.getString("players", "player");
			LinearLayout *line = statusVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

			LinearLayout *view = line->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, lineMargins)));
			view->SetBG(UI::Drawable(0xFF121212));
			TextView *GroupView = view->Add(new TextView(" " + GroupName, FLAG_DYNAMIC_ASCII, true));
			GroupView->SetTextColor(0xFF45BFCA);
			TextView *PlayersView = line->Add(new TextView(Players, FLAG_DYNAMIC_ASCII, true));
			PlayersView->SetTextColor(0xBBFFFFFF);
		}
	}
}

bool AmultiosOverlayScreen::touch(const TouchInput &touch)
{

	// if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0)
	// {
	// 	return UIDialogScreen::touch(touch);
	// }

	// if (!box_->GetBounds().Contains(touch.x, touch.y))
	// {
	// 	screenManager()->finishDialog(this, DR_BACK);
	// }

	return UIDialogScreen::touch(touch);
}

void AmultiosOverlayScreen::update()
{
	UIDialogScreen::update();
	alpha_ = 1.0f;

	if (scrollChat_ && toBottom_)
	{
		toBottom_ = false;
		scrollChat_->ScrollToBottom();
	}

	const float now = time_now();
	if (now > cmList.getLastUpdate() && cmList.getChatUpdate())
	{
		UpdateChat();
		cmList.doChatUpdate();
	}

	if (now > cmList.getLastPlayerStatusUpdate() && cmList.getPlayerStatusUpdate())
	{
		UpdateStatus();
		cmList.doPlayerStatusUpdate();
	}
}

void AmultiosOverlayScreen::CallbackDeleteConfig(bool yes)
{
	if (yes)
	{
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
		g_Config.unloadGameConfig();
		g_Config.deleteGameConfig(info->id);
		info->hasConfig = false;
		screenManager()->RecreateAllViews();
	}
}

UI::EventReturn AmultiosOverlayScreen::OnExitToMenu(UI::EventParams &e)
{
	if (g_Config.bPauseMenuExitsEmulator)
	{
		System_SendMessage("finish", "");
	}
	else
	{
		TriggerFinish(DR_OK);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn AmultiosOverlayScreen::OnCreateConfig(UI::EventParams &e)
{
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
	std::string gameId = g_paramSFO.GetDiscID();
	g_Config.createGameConfig(gameId);
	g_Config.changeGameSpecific(gameId, info->GetTitle());
	g_Config.saveGameConfig(gameId, info->GetTitle());
	if (info)
	{
		info->hasConfig = true;
	}

	screenManager()->topScreen()->RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn AmultiosOverlayScreen::OnDeleteConfig(UI::EventParams &e)
{
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	screenManager()->push(
		new PromptScreen(di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"), ga->T("ConfirmDelete"), di->T("Cancel"),
						 std::bind(&AmultiosOverlayScreen::CallbackDeleteConfig, this, std::placeholders::_1)));

	return UI::EVENT_DONE;
}

UI::EventReturn AmultiosOverlayScreen::OnGameSettings(UI::EventParams &e)
{
	screenManager()->push(new GameSettingsScreen(gamePath_));
	return UI::EVENT_DONE;
}

AmultiosOverlayScreen::~AmultiosOverlayScreen()
{
	cmList.shutdownPlayerStatus();
}
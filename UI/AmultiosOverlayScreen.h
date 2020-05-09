#pragma once

#include <functional>
#include <memory>
#include "file/file_util.h"
#include "ui/ui_screen.h"
class AmultiosOverlayScreen : public UIDialogScreen
{
public:
	AmultiosOverlayScreen(const std::string &filename) : UIDialogScreen(),gamePath_(filename){};
	virtual ~AmultiosOverlayScreen();
	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	bool isTransparent() const override { return true; }
	bool touch(const TouchInput &touch) override;
	void update() override;
	void UpdateChat();
	void UpdateStatus();
	void UpdateAccount();
	bool toBottom_;
protected:
	void CallbackDeleteConfig(bool yes);
private:
	bool FillVertical() const { return true; }
	UI::TabHolder *tabHolder_;
	UI::EventReturn OnSubmit(UI::EventParams &e);
	UI::TextEdit *chatEdit_;
	UI::ScrollView *scrollChat_;
	UI::TextView *currentChannel_;
	UI::LinearLayout *chatVert_ = nullptr;
	UI::LinearLayout *statusVert_ = nullptr;
	UI::LinearLayout *accountVert_ = nullptr;
	std::string gamePath_;
	UI::EventReturn OnCreateConfig(UI::EventParams &e);
	UI::EventReturn OnDeleteConfig(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnExitToMenu(UI::EventParams &e);
};
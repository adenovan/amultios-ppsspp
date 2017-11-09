#pragma once
#include "file/file_util.h"
#include "ui/ui_screen.h"
class ChatScreen : public UIDialogScreen {
public:
	ChatScreen();
	~ChatScreen();
	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	bool isTransparent() const override { return true; }
	bool touch(const TouchInput &touch) override;
	void update() override;
	void UpdateChat();
	bool toBottom_;
private:
	bool FillVertical() const { return false; }
	UI::Size ChatScreenWidth() const { return 550; }
	UI::Size ChatScreenHeight() const { return 360; }
	UI::EventReturn OnSubmit(UI::EventParams &e);
	UI::EventReturn OnChangeChannel(UI::EventParams &e);
	UI::EventReturn OnQuickChat1(UI::EventParams &e);
	UI::EventReturn OnQuickChat2(UI::EventParams &e);
	UI::EventReturn OnQuickChat3(UI::EventParams &e);
	UI::EventReturn OnQuickChat4(UI::EventParams &e);
	UI::EventReturn OnQuickChat5(UI::EventParams &e);
	UI::TextEdit *chatEdit_;
	UI::ScrollView *scroll_;
	UI::LinearLayout *chatVert_;
	UI::ViewGroup *box_;
};
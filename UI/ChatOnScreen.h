#pragma once
#include "file/file_util.h"
#include "ui/ui_screen.h"
class ChatOnScreen : public UIScreen {
public:
	ChatOnScreen();
	~ChatOnScreen();
	void CreateViews() override;
	bool isTransparent() const override { return true; }
	void update() override;
	void UpdateChat();
private:
	bool FillVertical() const { return false; }
	UI::Size ChatOnScreenWidth() const { return 550; }
	UI::Size ChatOnScreenHeight() const { return 450; }
	UI::LinearLayout *chatVert_;
	UI::ViewGroup *box_;
};
#pragma once
#include "ui/ui_screen.h"
class ChatOnScreen : public UI::InertView {
public:
	ChatOnScreen(UI::LayoutParams *layoutParams = nullptr) : UI::InertView(layoutParams) {}
	void Update() override;
private:
	bool FillVertical() const { return false; }
	UI::Size ChatOnScreenWidth() const { return 550; }
	UI::Size ChatOnScreenHeight() const { return 450; }
	UI::LinearLayout *chatVert_;
	UI::ViewGroup *box_;
};
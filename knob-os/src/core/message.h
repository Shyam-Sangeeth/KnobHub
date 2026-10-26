#pragma once
#include <functional>
#include <string>

#include "screens.h"

// A simple centred message, e.g. "Open Knob Hub on your PC". Pressing or going back runs on_back (default: pop).
class MessageScreen : public Screen {
public:
    MessageScreen(const std::string& icon, const std::string& title, const std::string& text, const std::string& hint,
                  std::function<void()> on_back = nullptr);
    void input(KnobInput in) override;

private:
    std::function<void()> on_back_;
};

#pragma once

// An app's "busy" spinner: while the app's screen data says busy (e.g. AudioDesk switching a device), a rainbow spinner
// turns in the middle of the screen, over everything, on a dimmed screen. Turns, presses and touches don't reach the
// screen meanwhile; Back (a long press or a swipe right) still does, so you can always leave.
namespace busy {

void set(bool on);
bool active();

}  // namespace busy

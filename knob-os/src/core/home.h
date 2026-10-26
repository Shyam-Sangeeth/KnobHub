#pragma once
#include <string>

#include "screens.h"

// The home screen: a ring of pages turned with the knob: Clock, the apps from Knob Hub and Settings, in the order Knob
// Hub sets (until it does, Clock first and Settings last). It rolls over from the last page to the first, both ways.
// Each app page shows the app's icon and name; pressing opens the app.
Screen* home_create();

// Rebuilds the pages after Knob Hub sent a new app list.
void home_refresh();

// Asks Knob Hub for the apps' icons at the sizes Home shows them, so the ring is complete without it next time.
void home_fetch_icons();

// The page on show: "clock", an app id or "settings"; and going straight to one (no animation).
std::string home_page();
void home_show(const std::string& id);

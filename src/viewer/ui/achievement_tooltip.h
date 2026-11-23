#pragma once

#include "viewer/achievements.h"

// Pathing's AchievementTooltipView, as text: the name, the description in grey under it, then
// the requirement — or the text of the marker's bit, when it names one that has text. Without
// the achievement category's icon, which would be one more texture per achievement.
namespace viewer::ui::achievement_tooltip {

// Draws into a window or tooltip already begun.
void draw(const achievements::Details& details, int bit);

} // namespace viewer::ui::achievement_tooltip

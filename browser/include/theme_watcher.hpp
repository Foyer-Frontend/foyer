#pragma once

namespace foyer::browser::theme_watcher {

// Kick a once-per-second poll of setsysGetColorSetId and feed any
// changes to brls::Application::getPlatform()->setThemeVariant.
// brls's SwitchPlatform reads ColorSetId only at init; without this
// poller the user has to relaunch foyer for a HOS Light↔Dark flip
// to take effect.
void start();

// Stop + delete the polling task. Called from HomeActivity's quit
// handler so the task can't fire after Application::quit() while
// brls is tearing down.
void stop();

} // namespace foyer::browser::theme_watcher

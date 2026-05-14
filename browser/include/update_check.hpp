#pragma once

#include <functional>

namespace foyer::browser::update_check {

// Self-update check for the foyer .nro itself.
//   - Silent path (boot): only react if a newer version is on the
//     manifest. Pop a brls::Dialog asking the user whether to
//     download. Yes → second job + restart prompt.
//   - Verbose path (Settings → "Check for foyer updates"): also
//     toast "up to date" when there's no newer version, and toast
//     on fetch failure.
//
// Module owns one job + one polling timer; calling either while
// one is in flight returns false. Settings tab + main both share
// this single helper so two simultaneous checks can't race the
// .nro download.
bool kick(bool verbose);

// Boot-time variant. Runs SYNCHRONOUSLY: fetches the manifest,
// shows the Yes/No dialog, optionally streams the download, and
// shows the restart prompt — invoking `on_done` exactly once when
// the user is back in control. Splash uses this to gate the
// handoff to Home so the boot doesn't continue underneath a pending
// "Download v0.6.x?" dialog (the v0.6.71-era bug: user clicks "Yes"
// but Home is already on screen, splash long gone).
//
// on_done is invoked on the brls main thread. Caller is responsible
// for not calling kick_boot twice; it's a one-shot per session.
void kick_boot(std::function<void()> on_done);

// Per-section content-update checks (cores / bezels / cheats).
// Each refreshes the matching manifest, aggregates pending updates
// against installed sidecars, and toasts a count summary. The four
// Settings → Updates rows each drive one of these — separated so
// users can poll just the bucket they care about.
enum class Section { Cores, Bezels, Cheats };
bool kick_content(Section s);

// Tear down the polling timer + abandon any in-flight worker.
// Called from HomeActivity's quit drain so brls doesn't fire a
// tick on a torn-down Application.
void stop();

}  // namespace foyer::browser::update_check

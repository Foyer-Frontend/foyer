#pragma once

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

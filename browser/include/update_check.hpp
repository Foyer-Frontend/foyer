#pragma once

namespace foyer::browser::update_check {

// Spawn a FoyerUpdateJob off-thread + poll it. On completion:
//   - Silent path (boot): only react if a newer version is on the
//     manifest. Pop a brls::Dialog asking the user whether to
//     download. Yes → second job + restart prompt.
//   - Verbose path (Settings → Check for updates): also toast
//     "up to date" when there's no newer version, and toast on
//     fetch failure.
//
// Module owns one job + one polling timer; calling either while
// one is in flight returns false. Settings tab + main both share
// this single helper so two simultaneous checks can't race the
// .nro download.
bool kick(bool verbose);

// Tear down the polling timer + abandon any in-flight worker.
// Called from HomeActivity's quit drain so brls doesn't fire a
// tick on a torn-down Application.
void stop();

}  // namespace foyer::browser::update_check

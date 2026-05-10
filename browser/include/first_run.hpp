#pragma once

namespace foyer::browser::first_run {

// Marker file at /foyer/data/first_run_complete. Created by the
// wizard's "Finish" step; once present, subsequent boots skip
// straight to HomeActivity.
bool is_complete();

// Idempotent — touches the marker file. Called from the wizard's
// Finish step.
void mark_complete();

// Removes the marker file so the wizard runs on the next launch.
// Used by Settings → "Re-run first-run wizard". Idempotent.
void reset();

}  // namespace foyer::browser::first_run

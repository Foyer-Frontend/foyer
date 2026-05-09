#pragma once

namespace foyer::browser::first_run {

// Marker file at /foyer/data/first_run_complete. Created by the
// wizard's "Finish" step; once present, subsequent boots skip
// straight to HomeActivity.
bool is_complete();

// Idempotent — touches the marker file. Called from the wizard's
// Finish step.
void mark_complete();

}  // namespace foyer::browser::first_run

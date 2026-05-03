#pragma once

#include "scanner.hpp"
#include "worker.hpp"

#include <string>

namespace foyer::library {

// Background per-system cover scrape. The worker walks every game in
// the snapshot, fetches the cover from the configured source, and
// reports progress via the Worker status string.
//
// We snapshot the system data into the job (folder/short names + game
// stems) rather than capture by reference because the UI is free to
// rescan the library while the worker is running.
class ScrapeJob {
public:
    enum class Source { Libretro, ScreenScraper, SteamGridDB };

    bool active()    const { return m_worker.active();    }
    bool done()      const { return m_worker.done();      }
    bool cancelled() const { return m_worker.cancelled(); }

    bool start(const System& sys, Source src);

    void cancel() { m_worker.cancel(); }
    std::string status_snapshot() const { return m_worker.status_snapshot(); }

    int hits()    const { return m_hits;    }
    int total()   const { return m_total;   }
    int done_ct() const { return m_done_ct; }

    void finish() { m_worker.finish(); }

private:
    Worker      m_worker;
    int         m_hits    = 0;
    int         m_total   = 0;
    int         m_done_ct = 0;
};

} // namespace foyer::library

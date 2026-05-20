#pragma once
//
// foyer-side theme-change broadcaster.
//
// brls's Platform::setThemeVariant flips the active variant but
// doesn't notify already-constructed views. brls-native theme attrs
// parsed from XML (textColor="@theme/brls/text" etc.) cache the
// resolved color at parse time — those are a brls limitation. foyer-
// owned widgets that read theme colors at construction (ActionButton's
// idle bg + icon tint, SystemTile's background, …) need a poke to
// re-sample on variant flip. theme_watcher::WatcherTask calls notify()
// each time it detects + applies a HOS Light↔Dark transition.
//
// Subscriber lifetime: each widget that registers MUST unsubscribe
// before destruction. Identifiers are integer-stable; unsubscribe is
// idempotent.
//
// Usage:
//   m_sub = theme_change::subscribe(
//       [this](brls::ThemeVariant v) { applyTheme(); });
//   // …
//   ~Widget() { theme_change::unsubscribe(m_sub); }

#include <borealis.hpp>

#include <functional>

namespace foyer::browser::theme_change {

using Hook = std::function<void(brls::ThemeVariant)>;

// Register a callback fired on the UI thread whenever the variant
// changes. Returns an opaque id used by unsubscribe(). The hook is
// NOT called immediately at subscribe time — the widget is expected
// to have already applied its initial colours.
int  subscribe(Hook hook);

// Drop a previously-registered callback. Pass -1 / a stale id to
// no-op safely.
void unsubscribe(int id);

// Fire every subscribed hook with the given variant. theme_watcher
// calls this after each setThemeVariant. Safe to call from any thread
// — implementation marshals to brls UI thread via brls::sync.
void notify(brls::ThemeVariant variant);

}  // namespace foyer::browser::theme_change

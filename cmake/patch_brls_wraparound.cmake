# Idempotent in-place patch to brls's Box::getNextFocus so a horizontal
# Box wraps left↔right at its edges instead of bouncing off. brls
# upstream chose hard-stop semantics; foyer wants carousel-style
# wrap on every horizontal nav (Home tiles, action rows, etc).
#
# Invocation:
#   cmake -DBOX_CPP=<path> -P patch_brls_wraparound.cmake
#
# Re-running is safe: a sentinel comment ("FOYER_WRAPAROUND") keeps
# the patch from stacking after subsequent configures.

if(NOT BOX_CPP OR NOT EXISTS "${BOX_CPP}")
    message(FATAL_ERROR "patch_brls_wraparound: BOX_CPP missing or not found (${BOX_CPP})")
endif()

file(READ "${BOX_CPP}" _content)

if(_content MATCHES "FOYER_WRAPAROUND")
    return()
endif()

# Exact text block we substitute. Matching by full block keeps us
# from accidentally rewriting some unrelated chunk of box.cpp.
set(_needle "    currentFocus = getParentNavigationDecision(this, currentFocus, direction);\n    if (!currentFocus && hasParent())\n        currentFocus = getParent()->getNextFocus(direction, this);\n    return currentFocus;\n}")

set(_replacement "    // FOYER_WRAPAROUND: when the nav walk fell off either edge of\n    // a horizontal Box, wrap by trying from the opposite end. Vertical\n    // boxes intentionally do NOT wrap (would surprise users on long\n    // settings lists). Only kicks in when the natural walk found no\n    // focusable view in the requested direction.\n    if (!currentFocus && this->axis == Axis::ROW && !this->children.empty()) {\n        size_t original = *((size_t*)parentUserData);\n        size_t idx = (offset == 1) ? 0 : this->children.size() - 1;\n        while (!currentFocus && idx != original && idx < this->children.size()) {\n            currentFocus = this->children[idx]->getDefaultFocus();\n            idx += offset;\n        }\n    }\n\n    currentFocus = getParentNavigationDecision(this, currentFocus, direction);\n    if (!currentFocus && hasParent())\n        currentFocus = getParent()->getNextFocus(direction, this);\n    return currentFocus;\n}")

string(FIND "${_content}" "${_needle}" _idx)
if(_idx EQUAL -1)
    message(WARNING "patch_brls_wraparound: needle not found — brls source may have moved; wrap-around left disabled")
    return()
endif()

string(REPLACE "${_needle}" "${_replacement}" _content "${_content}")
file(WRITE "${BOX_CPP}" "${_content}")
message(STATUS "patch_brls_wraparound: applied to ${BOX_CPP}")

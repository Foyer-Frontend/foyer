# Idempotent in-place fix for brls's SwitchInputManager::clearVibration:
# upstream calls sendRumbleInternal with (160.0f, 320.0f, 0.0f, 0.0f) which
# is interpreted as motor *frequencies*. The implementation only uses the
# == 0 / != 0 test to gate amplitude (non-zero → 320, zero → 0), so the
# "clear" call actually SETS amp_low = 320 on both motors and starts a
# continuous rumble on every controller the SwitchInputManager constructor
# touches.
#
# Symptom (foyer 0.7.15 and earlier): every Pro Controller in docked mode
# buzzes from app launch until HOS HOME suspend+resume clears it. Also
# fires on chain-launch back from a player nro.
#
# Fix: pass zeros to sendRumbleInternal so the amplitudes evaluate to 0.
#
# Invocation:
#   cmake -DINPUT_CPP=<path> -P patch_brls_rumble_clear.cmake
#
# Re-running is safe: a sentinel comment ("FOYER_RUMBLE_CLEAR") keeps the
# patch from stacking after subsequent configures.

if(NOT INPUT_CPP OR NOT EXISTS "${INPUT_CPP}")
    message(FATAL_ERROR
        "patch_brls_rumble_clear: INPUT_CPP missing or not found (${INPUT_CPP})")
endif()

file(READ "${INPUT_CPP}" _content)

if(_content MATCHES "FOYER_RUMBLE_CLEAR")
    return()
endif()

# Match the whole clearVibration body so the patch doesn't accidentally
# rewrite some other sendRumbleInternal call elsewhere in the file.
set(_needle "void SwitchInputManager::clearVibration(int controller)\n{\n    Logger::debug(\"Vibration clear #{}\", controller);\n    hidInitializeVibrationDevices(m_vibration_device_handles[controller], 2, (HidNpadIdType)controller, HidNpadStyleTag_NpadJoyDual);\n    sendRumbleInternal(m_vibration_device_handles[controller], m_vibration_values[controller], 160.0f, 320.0f, 0.0f, 0.0f);\n}")

set(_replacement "void SwitchInputManager::clearVibration(int controller)\n{\n    // FOYER_RUMBLE_CLEAR: upstream passes (160.0f, 320.0f, 0.0f, 0.0f) here\n    // but sendRumbleInternal interprets those args as motor frequencies +\n    // amplitude gates (!=0 -> amp 320), so the \"clear\" call actually starts\n    // full-strength rumble on amp_low of both motors. Every Pro Controller\n    // in docked mode buzzed from launch until HOS HOME resume. Pass zeros\n    // so the amplitudes evaluate to 0.\n    Logger::debug(\"Vibration clear #{}\", controller);\n    hidInitializeVibrationDevices(m_vibration_device_handles[controller], 2, (HidNpadIdType)controller, HidNpadStyleTag_NpadJoyDual);\n    sendRumbleInternal(m_vibration_device_handles[controller], m_vibration_values[controller], 0, 0, 0, 0);\n}")

string(FIND "${_content}" "${_needle}" _idx)
if(_idx EQUAL -1)
    message(WARNING
        "patch_brls_rumble_clear: needle not found — brls source may have moved; "
        "controllers will buzz on launch until the patch is updated")
    return()
endif()

string(REPLACE "${_needle}" "${_replacement}" _content "${_content}")
file(WRITE "${INPUT_CPP}" "${_content}")
message(STATUS "patch_brls_rumble_clear: applied to ${INPUT_CPP}")

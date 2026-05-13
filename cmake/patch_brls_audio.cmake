# Idempotent in-place patch to brls's SwitchAudioPlayer.
#
# XITRIX/borealis ships switch_audio.cpp with the actual audio init
# AND the play() implementation commented out — every brls sound
# (SOUND_FOCUS_CHANGE / SOUND_CLICK / SOUND_BACK / etc.) becomes a
# no-op even though libpulsar + the SOUNDS_MAP table are present.
# Uncomment both, leave a sentinel so reconfigures don't restack.
#
# Risk: SwitchAudioPlayer::SwitchAudioPlayer mounts qlaunch's
# program-romfs (PID 0x0100000000001000) to read /sound/qlaunch.bfsar.
# Needs fs:srv permission. The constructor handles failure cleanly
# (logs + leaves this->init=false), and play() short-circuits on
# !init — so applying the patch is safe; worst case the user gets
# silent UI just like before.
#
# Invocation:
#   cmake -DAUDIO_CPP=<path> -P patch_brls_audio.cmake

if(NOT AUDIO_CPP OR NOT EXISTS "${AUDIO_CPP}")
    message(FATAL_ERROR "patch_brls_audio: AUDIO_CPP missing or not found (${AUDIO_CPP})")
endif()

file(READ "${AUDIO_CPP}" _content)

if(_content MATCHES "FOYER_AUDIO_PATCH")
    return()
endif()

# Constructor body — replace the entire commented block + the
# closing brace with the uncommented version. We anchor on the
# first commented line of the init and the closing "}" of the
# constructor.
set(_ctor_needle "//    PLSR_RC rc = plsrPlayerInit();
//    if (PLSR_RC_FAILED(rc))
//    {
//        Logger::error(\"Unable to init Pulsar player: {:#x}\", rc);
//        return;
//    }
//
//    // Check if the program running is qlaunch
//    char bfsarPath[29];
//    u64 programId = 0;
//    svcGetInfo(&programId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
//    if (programId != QLAUNCH_PID)
//    {
//        // Mount qlaunch ROMFS for the BFSAR
//        Result result = romfsMountDataStorageFromProgram(QLAUNCH_PID, QLAUNCH_MOUNT_POINT);
//        if (!R_SUCCEEDED(result))
//        {
//            Logger::error(\"Unable to mount qlaunch ROMFS: {:#x}\", result);
//
//            plsrPlayerExit();
//            return;
//        }
//
//        sprintf(bfsarPath, \"%s:%s\", QLAUNCH_MOUNT_POINT, BFSAR_PATH);
//    }
//    else
//        sprintf(bfsarPath, \"%s:%s\", ROMFS_MOUNT_POINT, BFSAR_PATH);
//
//    // Open qlaunch BFSAR
//    rc = plsrBFSAROpen(bfsarPath, &this->qlaunchBfsar);
//    if (PLSR_RC_FAILED(rc))
//    {
//        Logger::error(\"Unable to open qlaunch BFSAR: {:#x}\", rc);
//
//        plsrPlayerExit();
//        return;
//    }
//
//    // Good to go~
//    this->init = true;")

set(_ctor_replacement "    // FOYER_AUDIO_PATCH — XITRIX commented these out; restore so
    // brls's UI sounds (focus / click / back) play through libpulsar
    // reading qlaunch's BFSAR. Failures fall through silently.
    PLSR_RC rc = plsrPlayerInit();
    if (PLSR_RC_FAILED(rc))
    {
        Logger::error(\"Unable to init Pulsar player: {:#x}\", rc);
        return;
    }

    char bfsarPath[29];
    u64 programId = 0;
    svcGetInfo(&programId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    if (programId != QLAUNCH_PID)
    {
        Result result = romfsMountDataStorageFromProgram(QLAUNCH_PID, QLAUNCH_MOUNT_POINT);
        if (!R_SUCCEEDED(result))
        {
            Logger::error(\"Unable to mount qlaunch ROMFS: {:#x}\", result);
            plsrPlayerExit();
            return;
        }
        sprintf(bfsarPath, \"%s:%s\", QLAUNCH_MOUNT_POINT, BFSAR_PATH);
    }
    else
        sprintf(bfsarPath, \"%s:%s\", ROMFS_MOUNT_POINT, BFSAR_PATH);

    rc = plsrBFSAROpen(bfsarPath, &this->qlaunchBfsar);
    if (PLSR_RC_FAILED(rc))
    {
        Logger::error(\"Unable to open qlaunch BFSAR: {:#x}\", rc);
        plsrPlayerExit();
        return;
    }

    this->init = true;")

string(FIND "${_content}" "${_ctor_needle}" _idx)
if(_idx EQUAL -1)
    message(WARNING "patch_brls_audio: ctor needle not found — brls source may have changed; audio left disabled")
    return()
endif()

string(REPLACE "${_ctor_needle}" "${_ctor_replacement}" _content "${_content}")

# play() body — same trick.
set(_play_needle "//    // Load the sound if needed
//    if (this->sounds[sound] == PLSR_PLAYER_INVALID_SOUND)
//    {
//        if (!this->load(sound))
//            return false;
//    }
//
//    // Play the sound
//    plsrPlayerSetPitch(this->sounds[sound], pitch);
//    PLSR_RC rc = plsrPlayerPlay(this->sounds[sound]);
//    if (PLSR_RC_FAILED(rc))
//    {
//        Logger::error(\"Unable to play sound {}: {:#x}\", sound, rc);
//        return false;
//    }
//
//    return true;
    return false;
}")

set(_play_replacement "    if (this->sounds[sound] == PLSR_PLAYER_INVALID_SOUND)
    {
        if (!this->load(sound))
            return false;
    }

    plsrPlayerSetPitch(this->sounds[sound], pitch);
    PLSR_RC rc = plsrPlayerPlay(this->sounds[sound]);
    if (PLSR_RC_FAILED(rc))
    {
        // (int)sound: brls::Sound is a plain enum that newer fmt
        // refuses to format without explicit conversion. Cast to
        // int so {} resolves to the integral formatter.
        Logger::error(\"Unable to play sound {}: {:#x}\", (int)sound, rc);
        return false;
    }

    return true;
}")

string(FIND "${_content}" "${_play_needle}" _play_idx)
if(NOT _play_idx EQUAL -1)
    string(REPLACE "${_play_needle}" "${_play_replacement}" _content "${_content}")
endif()

file(WRITE "${AUDIO_CPP}" "${_content}")
message(STATUS "patch_brls_audio: applied to ${AUDIO_CPP}")

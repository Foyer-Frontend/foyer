# cores/stella.cmake — libretro stella (Atari 2600) core build.
#
# Mirrors the source list in src/os/libretro/Makefile.common and the
# `platform=libnx` row of src/os/libretro/Makefile (STATIC_LINKING=1,
# DSWITCH=1 -D__SWITCH__ -DARM). The libretro core is C++ only — no
# Makefile.common SOURCES_C entries — and pins -std=c++17 -fno-rtti.

include(FetchContent)

FetchContent_Declare(libretro_stella
    GIT_REPOSITORY https://github.com/libretro/stella.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_stella)

set(_STELLA      ${libretro_stella_SOURCE_DIR})
set(_STELLA_SRC  ${_STELLA}/src)
set(_STELLA_LR   ${_STELLA_SRC}/os/libretro)

# ---------------------------------------------------------------------------
# Source list — matches Makefile.common SOURCES_CXX exactly.
# ---------------------------------------------------------------------------
set(_STELLA_LR_SRC
    ${_STELLA_LR}/libretro.cxx
    ${_STELLA_LR}/FSNodeLIBRETRO.cxx
    ${_STELLA_LR}/StellaLIBRETRO.cxx
)

set(_STELLA_COMMON_SRC
    ${_STELLA_SRC}/common/AudioQueue.cxx
    ${_STELLA_SRC}/common/AudioSettings.cxx
    ${_STELLA_SRC}/common/Base.cxx
    ${_STELLA_SRC}/common/Bezel.cxx
    ${_STELLA_SRC}/common/DevSettingsHandler.cxx
    ${_STELLA_SRC}/common/FpsMeter.cxx
    ${_STELLA_SRC}/common/FSNodeZIP.cxx
    ${_STELLA_SRC}/common/JoyMap.cxx
    ${_STELLA_SRC}/common/KeyMap.cxx
    ${_STELLA_SRC}/common/Logger.cxx
    ${_STELLA_SRC}/common/MouseControl.cxx
    ${_STELLA_SRC}/common/PaletteHandler.cxx
    ${_STELLA_SRC}/common/PhosphorHandler.cxx
    ${_STELLA_SRC}/common/PhysicalJoystick.cxx
    ${_STELLA_SRC}/common/PJoystickHandler.cxx
    ${_STELLA_SRC}/common/PKeyboardHandler.cxx
    ${_STELLA_SRC}/common/RewindManager.cxx
    ${_STELLA_SRC}/common/StaggeredLogger.cxx
    ${_STELLA_SRC}/common/StateManager.cxx
    ${_STELLA_SRC}/common/TimerManager.cxx
    ${_STELLA_SRC}/common/VideoModeHandler.cxx
    ${_STELLA_SRC}/common/tv_filters/AtariNTSC.cxx
    ${_STELLA_SRC}/common/tv_filters/NTSCFilter.cxx
    ${_STELLA_SRC}/common/repository/CompositeKeyValueRepository.cxx
    ${_STELLA_SRC}/common/repository/CompositeKVRJsonAdapter.cxx
    ${_STELLA_SRC}/common/repository/KeyValueRepositoryConfigfile.cxx
    ${_STELLA_SRC}/common/repository/KeyValueRepositoryJsonFile.cxx
    ${_STELLA_SRC}/common/repository/KeyValueRepositoryPropertyFile.cxx
)

set(_STELLA_EMUCORE_SRC
    ${_STELLA_SRC}/emucore/AtariVox.cxx
    ${_STELLA_SRC}/emucore/Bankswitch.cxx
    ${_STELLA_SRC}/emucore/Booster.cxx
    ${_STELLA_SRC}/emucore/Cart.cxx
    ${_STELLA_SRC}/emucore/CartCreator.cxx
    ${_STELLA_SRC}/emucore/CartDetector.cxx
    ${_STELLA_SRC}/emucore/CartEnhanced.cxx
    ${_STELLA_SRC}/emucore/Cart03E0.cxx
    ${_STELLA_SRC}/emucore/Cart0840.cxx
    ${_STELLA_SRC}/emucore/Cart0FA0.cxx
    ${_STELLA_SRC}/emucore/Cart2K.cxx
    ${_STELLA_SRC}/emucore/Cart3E.cxx
    ${_STELLA_SRC}/emucore/Cart3EPlus.cxx
    ${_STELLA_SRC}/emucore/Cart3EX.cxx
    ${_STELLA_SRC}/emucore/Cart3F.cxx
    ${_STELLA_SRC}/emucore/Cart4A50.cxx
    ${_STELLA_SRC}/emucore/Cart4K.cxx
    ${_STELLA_SRC}/emucore/Cart4KSC.cxx
    ${_STELLA_SRC}/emucore/CartAR.cxx
    ${_STELLA_SRC}/emucore/CartARM.cxx
    ${_STELLA_SRC}/emucore/CartBF.cxx
    ${_STELLA_SRC}/emucore/CartBFSC.cxx
    ${_STELLA_SRC}/emucore/CartBUS.cxx
    ${_STELLA_SRC}/emucore/CartCDF.cxx
    ${_STELLA_SRC}/emucore/CartCM.cxx
    ${_STELLA_SRC}/emucore/CartCTY.cxx
    ${_STELLA_SRC}/emucore/CartCV.cxx
    ${_STELLA_SRC}/emucore/CartDF.cxx
    ${_STELLA_SRC}/emucore/CartDFSC.cxx
    ${_STELLA_SRC}/emucore/CartDPC.cxx
    ${_STELLA_SRC}/emucore/CartDPCPlus.cxx
    ${_STELLA_SRC}/emucore/CartE0.cxx
    ${_STELLA_SRC}/emucore/CartE7.cxx
    ${_STELLA_SRC}/emucore/CartEF.cxx
    ${_STELLA_SRC}/emucore/CartEFSC.cxx
    ${_STELLA_SRC}/emucore/CartF0.cxx
    ${_STELLA_SRC}/emucore/CartF4.cxx
    ${_STELLA_SRC}/emucore/CartF4SC.cxx
    ${_STELLA_SRC}/emucore/CartF6.cxx
    ${_STELLA_SRC}/emucore/CartF6SC.cxx
    ${_STELLA_SRC}/emucore/CartF8.cxx
    ${_STELLA_SRC}/emucore/CartF8SC.cxx
    ${_STELLA_SRC}/emucore/CartFA2.cxx
    ${_STELLA_SRC}/emucore/CartFA.cxx
    ${_STELLA_SRC}/emucore/CartFC.cxx
    ${_STELLA_SRC}/emucore/CartFE.cxx
    ${_STELLA_SRC}/emucore/CartGL.cxx
    ${_STELLA_SRC}/emucore/CartMDM.cxx
    ${_STELLA_SRC}/emucore/CartMVC.cxx
    ${_STELLA_SRC}/emucore/CartSB.cxx
    ${_STELLA_SRC}/emucore/CartTVBoy.cxx
    ${_STELLA_SRC}/emucore/CartUA.cxx
    ${_STELLA_SRC}/emucore/CartWD.cxx
    ${_STELLA_SRC}/emucore/CartX07.cxx
    ${_STELLA_SRC}/emucore/CompuMate.cxx
    ${_STELLA_SRC}/emucore/Console.cxx
    ${_STELLA_SRC}/emucore/Control.cxx
    ${_STELLA_SRC}/emucore/ControllerDetector.cxx
    ${_STELLA_SRC}/emucore/DispatchResult.cxx
    ${_STELLA_SRC}/emucore/Driving.cxx
    ${_STELLA_SRC}/emucore/EmulationTiming.cxx
    ${_STELLA_SRC}/emucore/EmulationWorker.cxx
    ${_STELLA_SRC}/emucore/EventHandler.cxx
    ${_STELLA_SRC}/emucore/FBSurface.cxx
    ${_STELLA_SRC}/emucore/FrameBuffer.cxx
    ${_STELLA_SRC}/emucore/FSNode.cxx
    ${_STELLA_SRC}/emucore/Genesis.cxx
    ${_STELLA_SRC}/emucore/GlobalKeyHandler.cxx
    ${_STELLA_SRC}/emucore/Joy2BPlus.cxx
    ${_STELLA_SRC}/emucore/Joystick.cxx
    ${_STELLA_SRC}/emucore/Keyboard.cxx
    ${_STELLA_SRC}/emucore/KidVid.cxx
    ${_STELLA_SRC}/emucore/Lightgun.cxx
    ${_STELLA_SRC}/emucore/M6502.cxx
    ${_STELLA_SRC}/emucore/M6532.cxx
    ${_STELLA_SRC}/emucore/MD5.cxx
    ${_STELLA_SRC}/emucore/MindLink.cxx
    ${_STELLA_SRC}/emucore/MT24LC256.cxx
    ${_STELLA_SRC}/emucore/OSystem.cxx
    ${_STELLA_SRC}/emucore/Paddles.cxx
    ${_STELLA_SRC}/emucore/PlusROM.cxx
    ${_STELLA_SRC}/emucore/PointingDevice.cxx
    ${_STELLA_SRC}/emucore/Props.cxx
    ${_STELLA_SRC}/emucore/PropsSet.cxx
    ${_STELLA_SRC}/emucore/QuadTari.cxx
    ${_STELLA_SRC}/emucore/SaveKey.cxx
    ${_STELLA_SRC}/emucore/Serializer.cxx
    ${_STELLA_SRC}/emucore/Settings.cxx
    ${_STELLA_SRC}/emucore/Switches.cxx
    ${_STELLA_SRC}/emucore/System.cxx
    ${_STELLA_SRC}/emucore/Thumbulator.cxx
    ${_STELLA_SRC}/emucore/tia/AudioChannel.cxx
    ${_STELLA_SRC}/emucore/tia/Audio.cxx
    ${_STELLA_SRC}/emucore/tia/Background.cxx
    ${_STELLA_SRC}/emucore/tia/Ball.cxx
    ${_STELLA_SRC}/emucore/tia/DrawCounterDecodes.cxx
    ${_STELLA_SRC}/emucore/tia/frame-manager/AbstractFrameManager.cxx
    ${_STELLA_SRC}/emucore/tia/frame-manager/FrameLayoutDetector.cxx
    ${_STELLA_SRC}/emucore/tia/frame-manager/FrameManager.cxx
    ${_STELLA_SRC}/emucore/tia/frame-manager/JitterEmulation.cxx
    ${_STELLA_SRC}/emucore/tia/LatchedInput.cxx
    ${_STELLA_SRC}/emucore/tia/Missile.cxx
    ${_STELLA_SRC}/emucore/tia/AnalogReadout.cxx
    ${_STELLA_SRC}/emucore/tia/Player.cxx
    ${_STELLA_SRC}/emucore/tia/Playfield.cxx
    ${_STELLA_SRC}/emucore/TIASurface.cxx
    ${_STELLA_SRC}/emucore/tia/TIA.cxx
)

# ---------------------------------------------------------------------------
# Build the core. INCFLAGS mirrors Makefile.common (libretro-common is not
# referenced by the libretro target source list, only by the optional MSVC
# 2003 compat header path which we skip).
# ---------------------------------------------------------------------------
add_library(core_stella STATIC
    ${_STELLA_LR_SRC}
    ${_STELLA_COMMON_SRC}
    ${_STELLA_EMUCORE_SRC}
)
target_include_directories(core_stella PUBLIC
    ${_STELLA_SRC}
    ${_STELLA_LR}
    ${_STELLA_SRC}/emucore
    ${_STELLA_SRC}/emucore/tia
    ${_STELLA_SRC}/emucore/tia/frame-manager
    ${_STELLA_SRC}/common
    ${_STELLA_SRC}/common/audio
    ${_STELLA_SRC}/common/tv_filters
    ${_STELLA_SRC}/common/repository
    ${_STELLA_SRC}/common/repository/sqlite
    ${_STELLA_SRC}/lib/json
    ${_STELLA_SRC}/lib/httplib
    ${_STELLA_SRC}/lib/sqlite
)
target_compile_definitions(core_stella PRIVATE
    # Upstream Makefile.libretro always-on defines.
    __LIB_RETRO__=1
    __LIBRETRO__=1
    SOUND_SUPPORT=1
    HAVE_STDINT_H=1
    HAVE_STRINGS_H=1
    # Switch / libnx target identification — libnx row of Makefile.libretro
    # uses -DSWITCH=1 -D__SWITCH__ -DARM. Add the standard libretro Switch
    # set for parity with other foyer cores.
    SWITCH=1
    __SWITCH__=1
    HAVE_LIBNX=1
    ARM=1
    FRONTEND_SUPPORTS_RGB565=1
)
# Upstream pins -fno-rtti and -std=c++17 for the libretro target. -w mutes
# warnings consistent with the rest of foyer's core recipes.
target_compile_options(core_stella PRIVATE
    -w
    -fno-rtti
)
set_target_properties(core_stella PROPERTIES
    CXX_STANDARD              17
    CXX_STANDARD_REQUIRED     ON
    POSITION_INDEPENDENT_CODE ON)

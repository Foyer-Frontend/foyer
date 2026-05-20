# Adds a setTintColor()/clearTintColor() API to brls::Image so callers
# can tint a PNG/JPG/SVG at draw time. nanovg's image pattern fill
# multiplies the texture sample by paint.innerColor; brls leaves
# innerColor at white (no tint), which is fine for full-colour
# screenshots but useless for monochrome action icons that need to
# follow the active theme (light icon on dark mode, dark icon on
# light mode).
#
# Patch lives in two places:
#   - image.hpp: add tintColor / hasTint protected members + two
#                public setters
#   - image.cpp: Image::draw clones paint, overrides innerColor when
#                hasTint is true, fills with the cloned paint
#
# Idempotent: sentinel "FOYER_IMAGE_TINT" gates the outer check so
# re-configures don't double-apply.
#
# Invocation:
#   cmake -DIMAGE_HPP=<path> -DIMAGE_CPP=<path> -P patch_brls_image_tint.cmake

if(NOT IMAGE_HPP OR NOT EXISTS "${IMAGE_HPP}")
    message(FATAL_ERROR
        "patch_brls_image_tint: IMAGE_HPP missing or not found (${IMAGE_HPP})")
endif()
if(NOT IMAGE_CPP OR NOT EXISTS "${IMAGE_CPP}")
    message(FATAL_ERROR
        "patch_brls_image_tint: IMAGE_CPP missing or not found (${IMAGE_CPP})")
endif()

# ---- image.hpp ----------------------------------------------------------
file(READ "${IMAGE_HPP}" _hpp)
if(NOT _hpp MATCHES "FOYER_IMAGE_TINT")
    # 1a. Public setters anchored on the existing static create() line.
    set(_n1 "    static View* create();")
    set(_r1 "    // FOYER_IMAGE_TINT: tint a single-channel/icon PNG to follow the\n    // active theme. Default tintColor (white) is a no-op via nanovg's\n    // image-pattern multiplier; non-white innerColor multiplies the\n    // texture sample. clearTintColor() restores the default.\n    void setTintColor(NVGcolor c) { this->tintColor = c; this->hasTint = true; }\n    void clearTintColor() { this->hasTint = false; }\n\n    static View* create();")

    # 1b. Protected members anchored on the existing NVGpaint paint line.
    set(_n2 "    NVGpaint paint;")
    set(_r2 "    NVGpaint paint;\n    NVGcolor tintColor = { 1.0f, 1.0f, 1.0f, 1.0f };\n    bool hasTint = false;")

    string(FIND "${_hpp}" "${_n1}" _i1)
    string(FIND "${_hpp}" "${_n2}" _i2)
    if(_i1 EQUAL -1 OR _i2 EQUAL -1)
        message(WARNING "patch_brls_image_tint: hpp needles not found — image tint API not added")
    else()
        string(REPLACE "${_n1}" "${_r1}" _hpp "${_hpp}")
        string(REPLACE "${_n2}" "${_r2}" _hpp "${_hpp}")
        file(WRITE "${IMAGE_HPP}" "${_hpp}")
        message(STATUS "patch_brls_image_tint: applied to ${IMAGE_HPP}")
    endif()
endif()

# ---- image.cpp ----------------------------------------------------------
file(READ "${IMAGE_CPP}" _cpp)
if(NOT _cpp MATCHES "FOYER_IMAGE_TINT")
    set(_n3 "    nvgFillPaint(vg, a(this->paint));\n    nvgFill(vg);")
    set(_r3 "    // FOYER_IMAGE_TINT: clone paint + multiply texture sample by\n    // tintColor when hasTint is true. Default path leaves innerColor\n    // at white (no-op).\n    NVGpaint _p = this->paint;\n    if (this->hasTint)\n    {\n        _p.innerColor.r = this->tintColor.r;\n        _p.innerColor.g = this->tintColor.g;\n        _p.innerColor.b = this->tintColor.b;\n    }\n    nvgFillPaint(vg, a(_p));\n    nvgFill(vg);")

    string(FIND "${_cpp}" "${_n3}" _i3)
    if(_i3 EQUAL -1)
        message(WARNING "patch_brls_image_tint: cpp needle not found — tint not wired into draw()")
    else()
        string(REPLACE "${_n3}" "${_r3}" _cpp "${_cpp}")
        file(WRITE "${IMAGE_CPP}" "${_cpp}")
        message(STATUS "patch_brls_image_tint: applied to ${IMAGE_CPP}")
    endif()
endif()

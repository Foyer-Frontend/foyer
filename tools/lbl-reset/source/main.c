// foyer-lbl-reset — disables the Switch's RGB color adjustment + clears any
// applied tint. Run once if your screen has a stuck yellowish/greenish cast
// after a homebrew session.
//
// Calls the lbl service raw IPC commands:
//   23 (0x17) DisableRgbAdjustment
//   25 (0x19) SetRgbAdjustment(struct{f32 r,g,b}) → identity (1.0, 1.0, 1.0)

#include <stdio.h>
#include <string.h>
#include <switch.h>

typedef struct {
    float r;
    float g;
    float b;
} LblRgb;

static Result lblDisableRgbAdjustment_(void) {
    return serviceDispatch(lblGetServiceSession(), 23);
}

static Result lblSetRgbAdjustment_(const LblRgb* rgb) {
    return serviceDispatchIn(lblGetServiceSession(), 25, *rgb);
}

int main(int argc, char** argv) {
    consoleInit(NULL);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&pad);

    Result rc;

    rc = lblInitialize();
    if (R_FAILED(rc)) {
        printf("lblInitialize failed: 2%03d-%04d\n",
            R_MODULE(rc) + 2000, R_DESCRIPTION(rc));
        goto end;
    }

    LblRgb identity = { 1.0f, 1.0f, 1.0f };
    rc = lblSetRgbAdjustment_(&identity);
    printf("SetRgbAdjustment(1,1,1):  %s (rc=0x%X)\n",
        R_SUCCEEDED(rc) ? "ok" : "fail", rc);

    rc = lblDisableRgbAdjustment_();
    printf("DisableRgbAdjustment:     %s (rc=0x%X)\n",
        R_SUCCEEDED(rc) ? "ok" : "fail", rc);

    rc = lblSaveCurrentSetting();
    printf("SaveCurrentSetting:       %s (rc=0x%X)\n",
        R_SUCCEEDED(rc) ? "ok" : "fail", rc);

    rc = lblLoadCurrentSetting();
    printf("LoadCurrentSetting:       %s (rc=0x%X)\n",
        R_SUCCEEDED(rc) ? "ok" : "fail", rc);

    lblExit();

end:
    printf("\npress + to exit\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}

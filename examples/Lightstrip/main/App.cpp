// Copyright (c) 2015-2019 The HomeKit ADK Contributors
//
// Licensed under the Apache License, Version 2.0 (the “License”);
// you may not use this file except in compliance with the License.
// See [CONTRIBUTORS.md] for the list of HomeKit ADK project authors.

// An example that implements the light bulb HomeKit profile. It can serve as a basic implementation for
// any platform. The accessory logic implementation is reduced to internal state updates and log output.
//
// This implementation is platform-independent.
//
// The code consists of multiple parts:
//
//   1. The definition of the accessory configuration and its internal state.
//
//   2. Helper functions to load and save the state of the accessory.
//
//   3. The definitions for the HomeKit attribute database.
//
//   4. The callbacks that implement the actual behavior of the accessory, in this
//      case here they merely access the global accessory state variable and write
//      to the log to make the behavior easily observable.
//
//   5. The initialization of the accessory state.
//
//   6. Callbacks that notify the server in case their associated value has changed.

#define VOLTS       5
#define MILLIAMPS   3300
#define LED_PIN     12 // TODO: Why does naming this DATA_PIN cause compile errors?
#define NUM_LEDS    84
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

#define HUE_DEFAULT        125 // TODO: Use Kconfig.projbuild for these
#define SATURATION_DEFAULT 204
#define BRIGHTNESS_DEFAULT 120

#define FASTLED_INTERNAL // Disable "No hardware SPI pins defined.  All SPI
                         // access will default to bitbanged output" message
#include "FastLED.h"
#include "HAP.h"

#include "App.h"
#include "DB.h"
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Domain used in the key value store for application data.
 *
 * Purged: On factory reset.
 */
#define kAppKeyValueStoreDomain_Configuration ((HAPPlatformKeyValueStoreDomain) 0x00)

/**
 * Key used in the key value store to store the configuration state.
 *
 * Purged: On factory reset.
 */
#define kAppKeyValueStoreKey_Configuration_State ((HAPPlatformKeyValueStoreDomain) 0x00)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Lightstrip information.
 */
typedef struct {
    bool on;
    CHSV led;
} lightstrip;

/**
 * Global accessory configuration.
 */
typedef struct {
    struct {
        lightstrip current;
    } state;
    HAPAccessoryServerRef* server;
    HAPPlatformKeyValueStoreRef keyValueStore;
} AccessoryConfiguration;

static AccessoryConfiguration accessoryConfiguration;

CRGB leds[NUM_LEDS];

//----------------------------------------------------------------------------------------------------------------------

/**
 * Load the accessory state from persistent memory.
 */
static void LoadAccessoryState(void) {
    HAPPrecondition(accessoryConfiguration.keyValueStore);

    HAPError err;

    // Load persistent state if available
    bool found;
    size_t numBytes;

    err = HAPPlatformKeyValueStoreGet(
            accessoryConfiguration.keyValueStore,
            kAppKeyValueStoreDomain_Configuration,
            kAppKeyValueStoreKey_Configuration_State,
            &accessoryConfiguration.state,
            sizeof accessoryConfiguration.state,
            &numBytes,
            &found);

    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        HAPFatalError();
    }
    if (!found || numBytes != sizeof accessoryConfiguration.state) {
        if (found) {
            HAPLogError(&kHAPLog_Default, "Unexpected app state found in key-value store. Resetting to default.");
        }
        HAPRawBufferZero(&accessoryConfiguration.state, sizeof accessoryConfiguration.state);

        // Set defaults
        accessoryConfiguration.state.current.led = CHSV(HUE_DEFAULT, SATURATION_DEFAULT, BRIGHTNESS_DEFAULT);
    }
}

/**
 * Save the accessory state to persistent memory.
 */
static void SaveAccessoryState(void) {
    HAPPrecondition(accessoryConfiguration.keyValueStore);

    HAPError err;
    err = HAPPlatformKeyValueStoreSet(
            accessoryConfiguration.keyValueStore,
            kAppKeyValueStoreDomain_Configuration,
            kAppKeyValueStoreKey_Configuration_State,
            &accessoryConfiguration.state,
            sizeof accessoryConfiguration.state);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        HAPFatalError();
    }
}

//----------------------------------------------------------------------------------------------------------------------

/**
 * HomeKit accessory that provides the Light Bulb service.
 *
 * Note: Not constant to enable BCT Manual Name Change.
 */
static HAPAccessory accessory = { .aid = 1,
                                  .category = kHAPAccessoryCategory_Lighting,
                                  .name = "ESP32 Lightstrip",
                                  .manufacturer = "Zack Elia",
                                  .model = "Lightstrip1,1",
                                  .serialNumber = "39EBDB144C6B",
                                  .firmwareVersion = "1",
                                  .hardwareVersion = "1",
                                  .services = (const HAPService* const[]) { &accessoryInformationService,
                                                                            &hapProtocolInformationService,
                                                                            &pairingService,
                                                                            &lightBulbService,
                                                                            NULL },
                                  .callbacks = { .identify = IdentifyAccessory } };

//----------------------------------------------------------------------------------------------------------------------

HAP_RESULT_USE_CHECK
HAPError IdentifyAccessory(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPAccessoryIdentifyRequest* request HAP_UNUSED,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbOnRead(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPBoolCharacteristicReadRequest* request HAP_UNUSED,
        bool* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.current.on;
    HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, *value ? "true" : "false");

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbOnWrite(
        HAPAccessoryServerRef* server,
        const HAPBoolCharacteristicWriteRequest* request,
        bool value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, value ? "true" : "false");
    if (accessoryConfiguration.state.current.on != value) {
        accessoryConfiguration.state.current.on = value;

        if (value) {
            FastLED.setBrightness(accessoryConfiguration.state.current.led.value);
        } else {
            FastLED.setBrightness(0);
        }
        FastLED.show();

        SaveAccessoryState();
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbHueRead(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPFloatCharacteristicReadRequest* request HAP_UNUSED,
        float* value,
        void* _Nullable context HAP_UNUSED) {
    // HomeKit value is 0-360, FastLED is 0-255
    *value = (accessoryConfiguration.state.current.led.hue * 360) / 255;
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, *value);

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbHueWrite(
        HAPAccessoryServerRef* server,
        const HAPFloatCharacteristicWriteRequest* request,
        float value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, value);

    // HomeKit value is 0-360, FastLED is 0-255
    value = (value * 255) / 360;

    if (accessoryConfiguration.state.current.led.hue != value) {
        accessoryConfiguration.state.current.led.hue = value;

        fill_solid(leds, NUM_LEDS, accessoryConfiguration.state.current.led);
        FastLED.show();

        SaveAccessoryState();
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbSaturationRead(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPFloatCharacteristicReadRequest* request HAP_UNUSED,
        float* value,
        void* _Nullable context HAP_UNUSED) {
    // HomeKit value is 0-100, FastLED is 0-255
    *value = (accessoryConfiguration.state.current.led.saturation * 100) / 255;
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, *value);

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbSaturationWrite(
        HAPAccessoryServerRef* server,
        const HAPFloatCharacteristicWriteRequest* request,
        float value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, value);

    // HomeKit value is 0-100, FastLED is 0-255
    value = (value * 255) / 100;

    if (accessoryConfiguration.state.current.led.saturation != value) {
        accessoryConfiguration.state.current.led.saturation = value;

        fill_solid(leds, NUM_LEDS, accessoryConfiguration.state.current.led);
        FastLED.show();

        SaveAccessoryState();
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbBrightnessRead(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPIntCharacteristicReadRequest* request HAP_UNUSED,
        int* value,
        void* _Nullable context HAP_UNUSED) {
    // HomeKit value is 0-100, FastLED is 0-255
    *value = (accessoryConfiguration.state.current.led.saturation * 100) / 255;
    HAPLogInfo(&kHAPLog_Default, "%s: %d", __func__, *value);

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbBrightnessWrite(
        HAPAccessoryServerRef* server,
        const HAPIntCharacteristicWriteRequest* request,
        int value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %d", __func__, value);

    // HomeKit value is 0-100, FastLED is 0-255
    value = (value * 255) / 100;

    if (accessoryConfiguration.state.current.led.value != value) {
        accessoryConfiguration.state.current.led.value = value;

        fill_solid(leds, NUM_LEDS, accessoryConfiguration.state.current.led);
        FastLED.setBrightness(accessoryConfiguration.state.current.led.value);
        FastLED.show();

        SaveAccessoryState();
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }

    return kHAPError_None;
}

//----------------------------------------------------------------------------------------------------------------------

void AccessoryNotification(
        const HAPAccessory* accessory,
        const HAPService* service,
        const HAPCharacteristic* characteristic,
        void* ctx) {
    HAPLogInfo(&kHAPLog_Default, "Accessory Notification");

    HAPAccessoryServerRaiseEvent(accessoryConfiguration.server, characteristic, service, accessory);
}

void AppCreate(HAPAccessoryServerRef* server, HAPPlatformKeyValueStoreRef keyValueStore) {
    HAPPrecondition(server);
    HAPPrecondition(keyValueStore);

    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

    HAPRawBufferZero(&accessoryConfiguration, sizeof accessoryConfiguration);
    accessoryConfiguration.server = server;
    accessoryConfiguration.keyValueStore = keyValueStore;
    LoadAccessoryState();
}

void AppRelease(void) {
}

void AppAccessoryServerStart(void) {
    HAPAccessoryServerStart(accessoryConfiguration.server, &accessory);
}

//----------------------------------------------------------------------------------------------------------------------

void AccessoryServerHandleUpdatedState(HAPAccessoryServerRef* server, void* _Nullable context) {
    HAPPrecondition(server);
    HAPPrecondition(!context);

    switch (HAPAccessoryServerGetState(server)) {
        case kHAPAccessoryServerState_Idle: {
            HAPLogInfo(&kHAPLog_Default, "Accessory Server State did update: Idle.");
            return;
        }
        case kHAPAccessoryServerState_Running: {
            HAPLogInfo(&kHAPLog_Default, "Accessory Server State did update: Running.");
            return;
        }
        case kHAPAccessoryServerState_Stopping: {
            HAPLogInfo(&kHAPLog_Default, "Accessory Server State did update: Stopping.");
            return;
        }
    }
    HAPFatalError();
}

const HAPAccessory* AppGetAccessoryInfo() {
    return &accessory;
}

void AppInitialize(
        HAPAccessoryServerOptions* hapAccessoryServerOptions,
        HAPPlatform* hapPlatform,
        HAPAccessoryServerCallbacks* hapAccessoryServerCallbacks) {
    FastLED.setMaxPowerInVoltsAndMilliamps(VOLTS, MILLIAMPS);
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    // TODO: Used saved state instead?
    fill_solid(leds, NUM_LEDS, CHSV(HUE_DEFAULT, SATURATION_DEFAULT, BRIGHTNESS_DEFAULT));
    FastLED.show();
}

void AppDeinitialize() {
    /*no-op*/
}

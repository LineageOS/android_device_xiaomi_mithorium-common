/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Lights.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <fcntl.h>

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;

namespace aidl {
namespace android {
namespace hardware {
namespace light {

#define LED_PATH(led)                       "/sys/class/leds/" led "/"

static const std::string led_paths[] {
    [RED] = LED_PATH("red"),
    [GREEN] = LED_PATH("green"),
    [BLUE] = LED_PATH("blue"),
    [WHITE] = LED_PATH("white"),
};

static const std::string kLCDFile = "/sys/class/backlight/panel0-backlight/brightness";
static const std::string kLCDFile2 = "/sys/class/leds/lcd-backlight/brightness";
static const std::string kLCDMaxFile = "/sys/class/backlight/panel0-backlight/max_brightness";
static const std::string kLCDMaxFile2 = "/sys/class/leds/lcd-backlight/max_brightness";

#define AutoHwLight(light) {.id = (int)light, .type = light, .ordinal = 0}

// List of supported lights
const static std::vector<HwLight> kAvailableLights = {
    AutoHwLight(LightType::BACKLIGHT),
    AutoHwLight(LightType::BATTERY),
    AutoHwLight(LightType::NOTIFICATIONS)
};

Lights::Lights() {
    std::string tempstr;

    // Backlight
    mBacklightNode = !access(kLCDFile.c_str(), F_OK) ? kLCDFile : kLCDFile2;
    ReadFileToString(!access(kLCDFile.c_str(), F_OK) ? kLCDMaxFile : kLCDMaxFile2, &tempstr, true);
    if (!tempstr.empty())
        mBacklightMaxBrightness = std::stoi(tempstr);
    if (mBacklightMaxBrightness < 255)
        mBacklightMaxBrightness = 255;

    // LED
    mWhiteLed = !!access((led_paths[GREEN] + "brightness").c_str(), W_OK);
    mLedUseRedAsWhite = mWhiteLed && !access((led_paths[RED] + "brightness").c_str(), F_OK);
    if (!access(((mLedUseRedAsWhite ? led_paths[RED] : led_paths[WHITE]) + "blink").c_str(), W_OK))
        mLedBreathType = LedBreathType::BLINK;
    else if (!access(((mLedUseRedAsWhite ? led_paths[RED] : led_paths[WHITE]) + "breath").c_str(), W_OK))
        mLedBreathType = LedBreathType::BREATH;
    else
        mLedBreathType = LedBreathType::TRIGGER;
}

// AIDL methods
ndk::ScopedAStatus Lights::setLightState(int id, const HwLightState& state) {
    switch (id) {
        case (int)LightType::BACKLIGHT:
            WriteToFile(mBacklightNode, RgbaToBrightness(state.color) * mBacklightMaxBrightness / 0xFF);
            break;
        case (int)LightType::BATTERY:
            mBattery = state;
            handleSpeakerBatteryLocked();
            break;
        case (int)LightType::NOTIFICATIONS:
            mNotification = state;
            handleSpeakerBatteryLocked();
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight>* lights) {
    for (auto i = kAvailableLights.begin(); i != kAvailableLights.end(); i++) {
        lights->push_back(*i);
    }
    return ndk::ScopedAStatus::ok();
}

// device methods
void Lights::setSpeakerLightLocked(const HwLightState& state) {
    uint32_t alpha, red, green, blue;
    bool rc = true;

    // Extract brightness from AARRGGBB
    alpha = (state.color >> 24) & 0xFF;
    red = (state.color >> 16) & 0xFF;
    green = (state.color >> 8) & 0xFF;
    blue = state.color & 0xFF;

    // Scale RGB brightness if Alpha brightness is not 0xFF
    if (alpha != 0xFF) {
        red = (red * alpha) / 0xFF;
        green = (green * alpha) / 0xFF;
        blue = (blue * alpha) / 0xFF;
    }

    // Reset LED trigger
    if (mLedBreathType == LedBreathType::TRIGGER) {
        if (mWhiteLed) {
            rc = setLedTrigger(mLedUseRedAsWhite ? RED : WHITE, "none");
        } else {
            rc = setLedTrigger(RED, "none");
            rc &= setLedTrigger(GREEN, "none");
            rc &= setLedTrigger(BLUE, "none");
        }
    }

    switch (state.flashMode) {
        case FlashMode::HARDWARE:
        case FlashMode::TIMED:
            rc = true;
            if (mWhiteLed) {
                if (mLedBreathType == LedBreathType::TRIGGER && RgbaToBrightness(state.color) > 0)
                    rc &= setLedBrightness(mLedUseRedAsWhite ? RED : WHITE, RgbaToBrightness(state.color));
                rc &= setLedBreath(mLedUseRedAsWhite ? RED : WHITE, state);
            } else {
                if (!!red) {
                    if (mLedBreathType == LedBreathType::TRIGGER && red > 0)
                        rc &= setLedBrightness(RED, red);
                    rc &= setLedBreath(RED, state);
                }
                if (!!green) {
                    if (mLedBreathType == LedBreathType::TRIGGER && green > 0)
                        rc &= setLedBrightness(GREEN, green);
                    rc &= setLedBreath(GREEN, state);
                }
                if (!!blue) {
                    if (mLedBreathType == LedBreathType::TRIGGER && blue > 0)
                        rc &= setLedBrightness(BLUE, blue);
                    rc &= setLedBreath(BLUE, state);
                }
            }
            if (rc)
                break;
            // Set brightness if breath fails
            FALLTHROUGH_INTENDED;
        case FlashMode::NONE:
        default:
            if (mWhiteLed) {
                rc = setLedBrightness(mLedUseRedAsWhite ? RED : WHITE, RgbaToBrightness(state.color));
            } else {
                rc = setLedBrightness(RED, red);
                rc &= setLedBrightness(GREEN, green);
                rc &= setLedBrightness(BLUE, blue);
            }
            break;
    }

    return;
}

void Lights::handleSpeakerBatteryLocked() {
    if (IsLit(mBattery.color))
        return setSpeakerLightLocked(mBattery);
    else
        return setSpeakerLightLocked(mNotification);
}

bool Lights::setLedBreath(led_type led, const HwLightState& state) {
    bool blink = (state.flashOnMs != 0 && state.flashOffMs != 0);
    bool rc = false;

    switch (mLedBreathType) {
        case LedBreathType::BLINK:
            rc = WriteToFile(led_paths[led] + "blink", blink);
            break;
        case LedBreathType::BREATH:
            rc = WriteToFile(led_paths[led] + "breath", blink);
            break;
        case LedBreathType::TRIGGER:
            rc = setLedTrigger(led, "timer");
            rc &= WriteToFile(led_paths[led] + "delay_on", state.flashOnMs);
            rc &= WriteToFile(led_paths[led] + "delay_off", state.flashOffMs);
            break;
        default:
            break;
    }

    return rc;
}

bool Lights::setLedBrightness(led_type led, uint32_t value) {
    return WriteToFile(led_paths[led] + "brightness", value);
}

bool Lights::setLedTrigger(led_type led, std::string value) {
    return WriteStringToFile(value, led_paths[led] + "trigger");
}

// Utils
bool Lights::IsLit(uint32_t color) {
    return color & 0x00ffffff;
}

uint32_t Lights::RgbaToBrightness(uint32_t color) {
    // Extract brightness from AARRGGBB.
    uint32_t alpha = (color >> 24) & 0xFF;

    // Retrieve each of the RGB colors
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;

    // Scale RGB colors if a brightness has been applied by the user
    if (alpha != 0xFF) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }

    return (77 * red + 150 * green + 29 * blue) >> 8;
}

// Write value to path and close file.
bool Lights::WriteToFile(const std::string& path, uint32_t content) {
    return WriteStringToFile(std::to_string(content), path);
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl

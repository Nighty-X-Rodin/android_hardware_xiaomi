/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (C) 2022-2026 The LineageOS Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "android.hardware.vibrator-service.xiaomi"

#include <aidl/android/hardware/vibrator/Effect.h>
#include <android-base/logging.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "effect.h"

using aidl::android::hardware::vibrator::Effect;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

/* Default ~170 HZ sine waveforms */
static const int8_t effect_0[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const int8_t effect_1[] = {
    -1, -18, -35, -51, -66, -80, -93, -104, -113,
    -120, -125, -127, -127, -126, -122, -116, -108,
    -97, -85, -72, -57, -41, -25, -8, 9, 26, 42,
    58, 73, 86, 98, 108, 116, 122, 126, 127, 127,
    124, 119, 112, 103, 92, 79, 65, 50, 34, 17,
};

static const int8_t primitive_0[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const int8_t primitive_1[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const int8_t primitive_2[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const struct effect_stream default_effects[] = {
    {
        .effect_id = 0,
        .length = ARRAY_SIZE(effect_0),
        .play_rate_hz = 8000,
        .data = effect_0,
    },
    {
        .effect_id = 1,
        .length = ARRAY_SIZE(effect_1),
        .play_rate_hz = 8000,
        .data = effect_1,
    },
};

static const struct effect_stream default_primitives[] = {
    {
        .effect_id = 0,
        .length = ARRAY_SIZE(primitive_0),
        .play_rate_hz = 8000,
        .data = primitive_0,
    },
    {
        .effect_id = 1,
        .length = ARRAY_SIZE(primitive_1),
        .play_rate_hz = 8000,
        .data = primitive_1,
    },
    {
        .effect_id = 2,
        .length = ARRAY_SIZE(primitive_2),
        .play_rate_hz = 8000,
        .data = primitive_2,
    },
};

namespace {

const uint32_t kDefaultPlayRateHz = 24000;
const uint16_t kPrimitiveMask = (1 << 15);

std::unordered_map<uint32_t, effect_stream> sEffectStreams;
std::unordered_map<uint32_t, std::vector<int8_t>> sEffectFifoData;

std::unique_ptr<effect_stream> readEffectStreamFromFile(uint32_t uniqueEffectId) {
    std::filesystem::path filePath;

    uint32_t effectId = uniqueEffectId & ~kPrimitiveMask;

    if ((uniqueEffectId & kPrimitiveMask) != 0) {
        filePath = "/vendor/etc/vibrator/primitive_effect_" + std::to_string(effectId) + ".bin";
    } else {
        filePath = "/vendor/etc/vibrator/effect_" + std::to_string(effectId) + ".bin";
    }

    std::ifstream data(filePath, std::ios::in | std::ios::binary);
    if (!data.is_open()) {
        return nullptr;
    }

    std::uint32_t fileSize = std::filesystem::file_size(filePath);
    if (fileSize == 0) {
        return nullptr;
    }

    std::vector<int8_t> fifoData(fileSize);
    data.read(reinterpret_cast<char*>(fifoData.data()), fileSize);

    auto result = sEffectFifoData.emplace(uniqueEffectId, std::move(fifoData));

    return std::make_unique<effect_stream>(effectId, fileSize, kDefaultPlayRateHz,
                                           result.first->second.data());
}

std::unique_ptr<effect_stream> duplicateEffect(const effect_stream* effectStream,
                                               uint32_t newEffectId) {
    const std::uint32_t newEffectLength = effectStream->length * 4;
    std::vector<int8_t> fifoData(newEffectLength);

    std::copy(effectStream->data, effectStream->data + effectStream->length, fifoData.begin());
    std::copy(effectStream->data, effectStream->data + effectStream->length,
              fifoData.begin() + newEffectLength - effectStream->length);

    auto result = sEffectFifoData.emplace(newEffectId, std::move(fifoData));

    return std::make_unique<effect_stream>(newEffectId, newEffectLength, kDefaultPlayRateHz,
                                           result.first->second.data());
}

const struct effect_stream* getDefaultEffect(uint32_t effect_id) {
    if ((effect_id & 0x8000) != 0) {
        uint32_t prim_id = effect_id & 0x7fff;
        for (size_t i = 0; i < ARRAY_SIZE(default_primitives); i++) {
            if (prim_id == default_primitives[i].effect_id)
                return &default_primitives[i];
        }
    } else {
        for (size_t i = 0; i < ARRAY_SIZE(default_effects); i++) {
            if (effect_id == default_effects[i].effect_id)
                return &default_effects[i];
        }
    }
    return nullptr;
}

}  // namespace

const struct effect_stream* get_effect_stream(uint32_t effectId) {
    auto it = sEffectStreams.find(effectId);
    if (it != sEffectStreams.end()) {
        return &it->second;
    }

    std::unique_ptr<effect_stream> newEffectStream = readEffectStreamFromFile(effectId);

    if (newEffectStream) {
        auto result = sEffectStreams.emplace(effectId, *newEffectStream);
        return &result.first->second;
    } else if (effectId == (uint32_t)Effect::DOUBLE_CLICK) {
        newEffectStream = duplicateEffect(get_effect_stream((uint32_t)Effect::CLICK),
                                          (uint32_t)Effect::DOUBLE_CLICK);
        if (newEffectStream) {
            auto result = sEffectStreams.emplace(effectId, *newEffectStream);
            return &result.first->second;
        }
    }

    // Fall back to built-in default waveform if no file found
    const struct effect_stream* def = getDefaultEffect(effectId);
    if (def != nullptr) {
        return def;
    }

    if (effectId != (uint32_t)Effect::CLICK) {
        return get_effect_stream((uint32_t)Effect::CLICK);
    }

    return nullptr;
}

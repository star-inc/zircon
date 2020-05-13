// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_AUDIO_SINE_SOURCE_H_
#define ZIRCON_SYSTEM_UAPP_AUDIO_SINE_SOURCE_H_

#include <math.h>

#include "generated-source.h"

class SineSource : public GeneratedSource {
 public:
  zx_status_t Init(float freq, float amp, float duration_secs, uint32_t frame_rate,
                   uint32_t channels, uint32_t active,
                   audio_sample_format_t sample_format) override {
    auto status = GeneratedSource::Init(freq, amp, duration_secs, frame_rate, channels, active,
                                        sample_format);

    pos_scalar_ = (freq * 2.0 * M_PI) / frame_rate;

    return status;
  }

  double GenerateValue(double pos) override { return sin(pos); }
};

#endif  // ZIRCON_SYSTEM_UAPP_AUDIO_SINE_SOURCE_H_

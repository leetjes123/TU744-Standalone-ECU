#pragma once

// Narrowband oxygen-sensor maths, kept free of UI dependencies so it can be
// unit tested against the firmware's own scaling.

// The O2 input is a 10-bit conversion over 0-5 V. This must match the firmware's
// stftMv5ToCounts() in SaxoM744Standalone/main.c, which turns the calibrated
// mV/5 thresholds (0x8C3 / 0x8C4) into counts as mv5 * 1023 / 1000 - i.e. the
// same 1023 counts = 5000 mV full scale.
inline float O2Volts(unsigned short adc) {
    return adc * (5.0f / 1023.0f);
}

// Indicative AFR from a narrowband sensor's output voltage. A zirconia sensor
// only resolves mixture within a narrow band around stoichiometric - the curve
// is near-vertical there and flat outside it - so this is a reading aid, never
// a substitute for a wideband. Values clamp to the ends of the curve.
inline float NarrowbandApproxAFR(float volts) {
    // Typical warm zirconia sensor transfer curve.
    static const float kVolts[] = { 0.00f, 0.10f, 0.20f, 0.30f, 0.40f, 0.45f,
                                    0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f };
    static const float kAfr[]   = { 17.0f, 16.0f, 15.4f, 15.0f, 14.9f, 14.7f,
                                    14.5f, 14.2f, 13.9f, 13.5f, 13.0f, 12.0f };
    const int count = (int)(sizeof(kVolts) / sizeof(kVolts[0]));
    if (volts <= kVolts[0]) return kAfr[0];
    if (volts >= kVolts[count - 1]) return kAfr[count - 1];
    for (int i = 1; i < count; ++i) {
        if (volts <= kVolts[i]) {
            const float span = kVolts[i] - kVolts[i - 1];
            const float t = span > 0.0f ? (volts - kVolts[i - 1]) / span : 0.0f;
            return kAfr[i - 1] + (kAfr[i] - kAfr[i - 1]) * t;
        }
    }
    return kAfr[count - 1];
}

// The band where a narrowband reading is actually informative.
inline bool NarrowbandAfrTrustworthy(float volts) {
    return volts >= 0.40f && volts <= 0.55f;
}

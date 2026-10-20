#ifndef EXPOSURE_GLSL
#define EXPOSURE_GLSL

const uint EXPOSURE_BIN_COUNT = 256u;
const float EXPOSURE_MIN_LOG2 = -16.0;
const float EXPOSURE_MAX_LOG2 = 6.0;
const float EXPOSURE_KEY_LOG2 = -3.2630344;

layout(buffer_reference, scalar) buffer ExposureBuffer {
    float correction_ev;
    float scale;
    float measured_log2;
    uint _pad;
    uint bins[EXPOSURE_BIN_COUNT];
};

uint exposure_bin(float p_luminance) {
    if (p_luminance < exp2(EXPOSURE_MIN_LOG2)) return 0u;
    const float t = (log2(p_luminance) - EXPOSURE_MIN_LOG2) / (EXPOSURE_MAX_LOG2 - EXPOSURE_MIN_LOG2);
    return 1u + uint(clamp(t, 0.0, 1.0) * float(EXPOSURE_BIN_COUNT - 2u));
}

float exposure_bin_log2(uint p_bin) {
    if (p_bin == 0u) return EXPOSURE_MIN_LOG2;
    return EXPOSURE_MIN_LOG2 + (float(p_bin - 1u) + 0.5) / float(EXPOSURE_BIN_COUNT - 2u) * (EXPOSURE_MAX_LOG2 - EXPOSURE_MIN_LOG2);
}

#endif
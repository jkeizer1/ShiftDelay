#include "api.h"
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <new>
#include <cstdio>

// Forward declaration
float clamp(float value, float min, float max);

float simple_fmodf(float x, float y) {
    return x - y * static_cast<float>(std::floor(static_cast<double>(x / y)));
}

struct ShiftDelay : public _NT_algorithm {
    ShiftDelay() : delayBuffer(nullptr),
                   writeIndex(0),
                   smoothedDelayTimeSec(0.01f),
                   smoothedBaseDelaySec(2.0f),
                   smoothedWetMix(0.5f),
                   previousDelayPotValue(2.0f),
                   stableDelayPotValue(2.0f),
                   stableSampleCount(0),
                   crossfadeActive(false),
                   oldDelaySec(0.0f),
                   crossfadeSampleCount(0) {}
    ~ShiftDelay() {}

    static constexpr float maxDelaySec = 6.0f;
    static constexpr size_t maxDelaySamples = static_cast<size_t>(maxDelaySec * 192000.0f);
    float* delayBuffer;
    size_t writeIndex;
    float smoothedDelayTimeSec;
    float smoothedBaseDelaySec;
    float smoothedWetMix;
    float previousDelayPotValue;
    float stableDelayPotValue;
    int stableSampleCount;
    bool crossfadeActive;
    float oldDelaySec;
    int crossfadeSampleCount;
    static constexpr float stabilizationTimeSec = 0.02f;
    static constexpr float baseSmoothingTimeSec = 0.002f;
    static constexpr float potSmoothingTimeSec = 0.05f;
    static constexpr float crossfadeTimeSec = 0.05f;
};

// Parameter indices
enum {
    kParamAudioInput,
    kParamPhaseCVInput,
    kParamDryWetCVInput,
    kParamPitchCVInput,
    kParamOutput,
    kParamOutputMode,
    kParamPhaseOffset,
    kParamDelayTime,
    kParamWetMix,
    kParamPitchCV,
};

// Enum strings for Output Mode
static const char* const enumStringsOutputMode[] = {
    "Add",
    "Replace",
    nullptr  // Null terminator
};

static const _NT_parameter shiftDelayParameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Audio Input", 1, 1)
    NT_PARAMETER_CV_INPUT("Phase CV Input", 0, 2)
    NT_PARAMETER_CV_INPUT("Dry/Wet CV Input", 0, 3)
    NT_PARAMETER_CV_INPUT("Pitch CV Input", 0, 4)
    NT_PARAMETER_AUDIO_OUTPUT("Output", 1, 1)
    { "Output Mode", 0, 1, 0, kNT_unitEnum, kNT_scalingNone, enumStringsOutputMode },
    { "Phase Offset", -360, 360, 0, kNT_unitNone, kNT_scalingNone, nullptr },
    { "Delay Time", 5, 6000, 2000, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Dry/Wet Mix", 0, 100, 50, kNT_unitPercent, kNT_scalingNone, nullptr },
    { "Pitch CV", -5, 5, 0, kNT_unitVolts, kNT_scalingNone, nullptr }
};

// Parameter pages
static const uint8_t controlsPageParams[] = { kParamPhaseOffset, kParamDelayTime, kParamWetMix, kParamPitchCV };
static const uint8_t routingPageParams[] = { kParamAudioInput, kParamPhaseCVInput, kParamDryWetCVInput, kParamPitchCVInput, kParamOutput, kParamOutputMode };

static const _NT_parameterPage shiftDelayParameterPages[] = {

    { "Controls", ARRAY_SIZE(controlsPageParams), controlsPageParams },
    { "Routing", ARRAY_SIZE(routingPageParams), routingPageParams }

};

static const _NT_parameterPages shiftDelayParameterPagesStruct = {

    ARRAY_SIZE(shiftDelayParameterPages),
    shiftDelayParameterPages

};

void calculateRequirementsShiftDelay(_NT_algorithmRequirements& req, const int32_t* specifications) {

    req.numParameters = ARRAY_SIZE(shiftDelayParameters);
    req.sram = sizeof(ShiftDelay);
    req.dram = ShiftDelay::maxDelaySamples * sizeof(float);
    req.dtc = 0;
    req.itc = 0;

}

_NT_algorithm* constructShiftDelay(const _NT_algorithmMemoryPtrs& ptrs, 
                                   const _NT_algorithmRequirements& req,
                                   const int32_t* specifications) {

    ShiftDelay* alg = new (static_cast<void*>(ptrs.sram)) ShiftDelay();
    alg->delayBuffer = reinterpret_cast<float*>(ptrs.dram);
    alg->parameters = shiftDelayParameters;
    alg->parameterPages = &shiftDelayParameterPagesStruct;
    
    // Initialize delay buffer
    if (alg->delayBuffer) {
        for (size_t i = 0; i < ShiftDelay::maxDelaySamples; ++i) {
            alg->delayBuffer[i] = 0.0f;
        }
    }
    
    return alg;
}

float pitchCVToFrequency(float cv) {

    return 440.0f * std::pow(2.0f, cv);
}

float computeDelayTime(float phaseOffsetDeg, float freqHz) {

    if (freqHz < 20.0f) return 0.0f;
    return (phaseOffsetDeg / 360.0f) / freqHz;

}

/*
float readDelayInterpolated(ShiftDelay* alg, float delayTimeSec, float sampleRate) {

    if (delayTimeSec < 0.0f) delayTimeSec = 0.0f;
    
    float delaySamples = delayTimeSec * sampleRate;
    if (delaySamples >= static_cast<float>(ShiftDelay::maxDelaySamples)) {
        delaySamples = static_cast<float>(ShiftDelay::maxDelaySamples) - 1.0f;
    }
    
    float readPos = static_cast<float>(alg->writeIndex) - delaySamples;
    if (readPos < 0.0f) {
        readPos += static_cast<float>(ShiftDelay::maxDelaySamples);
    }
    
    int index1 = static_cast<int>(readPos) % ShiftDelay::maxDelaySamples;
    int index2 = (index1 + 1) % ShiftDelay::maxDelaySamples;
    float frac = readPos - static_cast<float>(static_cast<int>(readPos));
    
    return (1.0f - frac) * alg->delayBuffer[index1] + frac * alg->delayBuffer[index2];
}
*/

// The following code reimplements negative shifting and multiple buffer wraps
float readDelayInterpolated(ShiftDelay* alg, float delayTimeSec, float sampleRate) {
    // Allow negative delays but prevent extreme values that could cause excessive looping
    const int maxWraps = 10; // Reasonable limit to prevent infinite loops
    const float maxDelayTime = static_cast<float>(ShiftDelay::maxDelaySamples * maxWraps) / sampleRate;

    if (delayTimeSec > maxDelayTime) {
        delayTimeSec = maxDelayTime;
    } else if (delayTimeSec < -maxDelayTime) {
        delayTimeSec = -maxDelayTime;
    }

    float delaySamples = delayTimeSec * sampleRate;
    float readPos = static_cast<float>(alg->writeIndex) - delaySamples;

    // Handle multiple buffer wraps with safety counter
    int wrapCount = 0;
    while (readPos < 0.0f && wrapCount < maxWraps) {
        readPos += static_cast<float>(ShiftDelay::maxDelaySamples);
        wrapCount++;
    }

    while (readPos >= static_cast<float>(ShiftDelay::maxDelaySamples) && wrapCount < maxWraps) {
        readPos -= static_cast<float>(ShiftDelay::maxDelaySamples);
        wrapCount++;
    }

    // Final safety clamp in case we hit the wrap limit
    if (readPos < 0.0f) {
        readPos = 0.0f;
    } else if (readPos >= static_cast<float>(ShiftDelay::maxDelaySamples)) {
        readPos = static_cast<float>(ShiftDelay::maxDelaySamples) - 1.0f;
    }

    int index1 = static_cast<int>(readPos) % ShiftDelay::maxDelaySamples;
    int index2 = (index1 + 1) % ShiftDelay::maxDelaySamples;
    float frac = readPos - static_cast<int>(readPos);

    return (1.0f - frac) * alg->delayBuffer[index1] + frac * alg->delayBuffer[index2];
}

void stepShiftDelay(_NT_algorithm* self, float* busFrames, int numFramesBy4) {

    ShiftDelay* alg = static_cast<ShiftDelay*>(self);

    int numFrames = numFramesBy4 * 4;
    float sampleRate = static_cast<float>(NT_globals.sampleRate);

    // Validate sample rate and delay buffer
    if (sampleRate < 32000.0f || sampleRate > 192000.0f || !alg->delayBuffer) {
        return; // Early exit if invalid
    }

    // Get bus indices (1-based in parameters, 0-based for array access)
    int audioInputBus = (alg->v[kParamAudioInput] > 0 && alg->v[kParamAudioInput] <= 28) ? 
                        alg->v[kParamAudioInput] - 1 : 0;

    int phaseCVBus = (alg->v[kParamPhaseCVInput] > 0 && alg->v[kParamPhaseCVInput] <= 28) ? 
                     alg->v[kParamPhaseCVInput] - 1 : 1;

    int dryWetCVBus = (alg->v[kParamDryWetCVInput] > 0 && alg->v[kParamDryWetCVInput] <= 28) ? 
                      alg->v[kParamDryWetCVInput] - 1 : 2;

    int pitchCVBus = (alg->v[kParamPitchCVInput] > 0 && alg->v[kParamPitchCVInput] <= 28) ? 
                     alg->v[kParamPitchCVInput] - 1 : 3;

    int outputBus = (alg->v[kParamOutput] > 0 && alg->v[kParamOutput] <= 28) ? 
                    alg->v[kParamOutput] - 1 : 0;
    
    bool replace = (alg->v[kParamOutputMode] == 1);

    // Get pointers to bus data
    const float* audioInput = busFrames + audioInputBus * numFrames;
    const float* phaseCVInput = busFrames + phaseCVBus * numFrames;
    const float* dryWetCVInput = busFrames + dryWetCVBus * numFrames;
    const float* pitchCVInput = busFrames + pitchCVBus * numFrames;
    float* output = busFrames + outputBus * numFrames;

    // Get base parameter values
    float basePhaseOffset = static_cast<float>(alg->v[kParamPhaseOffset]);
    float currentDelayPotValue = static_cast<float>(alg->v[kParamDelayTime]) / 1000.0f; // Convert ms to seconds
    float baseWetMix = static_cast<float>(alg->v[kParamWetMix]) / 100.0f; // Convert percentage to 0-1
    float basePitchCV = static_cast<float>(alg->v[kParamPitchCV]);

    // Handle delay pot stabilization
/*
    bool potChanged = (currentDelayPotValue != alg->previousDelayPotValue);
    if (potChanged) {
        alg->stableSampleCount = 0;
        alg->previousDelayPotValue = currentDelayPotValue;
    } else {
        alg->stableSampleCount += numFrames;
        int samplesForStabilization = static_cast<int>(alg->stabilizationTimeSec * sampleRate);
        if (alg->stableSampleCount >= samplesForStabilization && !alg->crossfadeActive) {
            alg->crossfadeActive = true;
            alg->crossfadeSampleCount = 0;
            alg->oldDelaySec = alg->smoothedBaseDelaySec;
            alg->stableDelayPotValue = currentDelayPotValue;
        }
    }
*/
    bool potChanged = (currentDelayPotValue != alg->previousDelayPotValue);
    if (potChanged) {
        alg->stableSampleCount = 0;
        alg->previousDelayPotValue = currentDelayPotValue;
    } else {
        alg->stableSampleCount += numFrames;
        int samplesForStabilization = static_cast<int>(alg->stabilizationTimeSec * sampleRate);
        if (alg->stableSampleCount >= samplesForStabilization && !alg->crossfadeActive) {
            alg->crossfadeActive = true;
            alg->crossfadeSampleCount = 0;
            alg->oldDelaySec = alg->smoothedBaseDelaySec;
            alg->stableDelayPotValue = currentDelayPotValue;
            // Optional: Set to exact value to prevent re-triggering
            alg->stableSampleCount = samplesForStabilization;
        }
    }
    // Smooth base delay
    float baseDelaySecTarget = clamp(alg->stableDelayPotValue, 0.005f, alg->maxDelaySec);
    float baseSmoothingCoeff = std::exp(-1.0f / (alg->baseSmoothingTimeSec * sampleRate));
    float potSmoothingCoeff = std::exp(-1.0f / (alg->potSmoothingTimeSec * sampleRate));
    float smoothingCoeff = (alg->stableSampleCount >= static_cast<int>(alg->stabilizationTimeSec * sampleRate))
                          ? potSmoothingCoeff : baseSmoothingCoeff;
    alg->smoothedBaseDelaySec = smoothingCoeff * alg->smoothedBaseDelaySec + (1.0f - smoothingCoeff) * baseDelaySecTarget;

    // Process each frame
    for (int i = 0; i < numFrames; ++i) {
        // Get CV values (handle case where CV inputs might be 0/disabled)
        float phaseCV = (alg->v[kParamPhaseCVInput] > 0) ? phaseCVInput[i] : 0.0f;
        float dryWetCV = (alg->v[kParamDryWetCVInput] > 0) ? dryWetCVInput[i] : 0.0f;
        float pitchCVMod = (alg->v[kParamPitchCVInput] > 0) ? pitchCVInput[i] : 0.0f;
        
        // Combine base parameters with CV inputs
        float phaseShiftDegrees = basePhaseOffset + (phaseCV * 72.0f); // Scale CV to degrees
        float wetMix = clamp(baseWetMix + (dryWetCV * 0.1f), 0.0f, 1.0f);
        float pitchCV = basePitchCV + pitchCVMod;

        // Smooth wet mix
        alg->smoothedWetMix = baseSmoothingCoeff * alg->smoothedWetMix + (1.0f - baseSmoothingCoeff) * wetMix;


        // Calculate frequency and delay time
        float freqHz = pitchCVToFrequency(pitchCV);
        if (freqHz < 0.1f) freqHz = 0.1f;

        float delayTimeTarget = computeDelayTime(phaseShiftDegrees, freqHz);
        float maxDelaySecActual = static_cast<float>(alg->maxDelaySamples) / sampleRate;

        // Combine phase delay with base delay (like VCV Rack)
        float totalDelayTime = delayTimeTarget + alg->smoothedBaseDelaySec;
        totalDelayTime = clamp(totalDelayTime, -maxDelaySecActual, maxDelaySecActual);

        // Smooth the TOTAL delay time (not just base)
        alg->smoothedDelayTimeSec = baseSmoothingCoeff * alg->smoothedDelayTimeSec +
        (1.0f - baseSmoothingCoeff) * totalDelayTime;

        // Get input sample
        float inputSample = audioInput[i];
        
        // Read delayed sample
        float outputSample;
        if (alg->crossfadeActive) {
            int crossfadeSamples = static_cast<int>(alg->crossfadeTimeSec * sampleRate);
            if (alg->crossfadeSampleCount < crossfadeSamples) {
                float t = static_cast<float>(alg->crossfadeSampleCount) / static_cast<float>(crossfadeSamples);

                // Calculate old total delay (phase + old base delay)
                float oldTotalDelay = delayTimeTarget + alg->oldDelaySec;
                oldTotalDelay = clamp(oldTotalDelay, -maxDelaySecActual, maxDelaySecActual);

                float oldOutput = readDelayInterpolated(alg, oldTotalDelay, sampleRate);
                float newOutput = readDelayInterpolated(alg, alg->smoothedDelayTimeSec, sampleRate);

                outputSample = (1.0f - t) * oldOutput + t * newOutput;
                alg->crossfadeSampleCount++;
            } else {
                alg->crossfadeActive = false;
                outputSample = readDelayInterpolated(alg, alg->smoothedDelayTimeSec, sampleRate);
            }
        } else {
            outputSample = readDelayInterpolated(alg, alg->smoothedDelayTimeSec, sampleRate);
        }

        // Mix dry and wet signals
        float finalOutput = alg->smoothedWetMix * outputSample + (1.0f - alg->smoothedWetMix) * inputSample;

        // Output based on mode
        if (replace) {
            output[i] = finalOutput;
        } else {
            output[i] += finalOutput;
        }

        // Write input to delay buffer
        alg->delayBuffer[alg->writeIndex] = inputSample;
        alg->writeIndex = (alg->writeIndex + 1) % alg->maxDelaySamples;
    }
}

bool drawShiftDelay(_NT_algorithm* self) {

    ShiftDelay* alg = static_cast<ShiftDelay*>(self);
    NT_drawText(10, 30, "ShiftDelay", 15, kNT_textLeft, kNT_textNormal);


    // Show current phase
    char phaseStr[32];
    int phaseDegrees = alg->v[kParamPhaseOffset];
    NT_intToString(phaseStr, phaseDegrees);
    NT_drawText(10, 45, phaseStr, 15, kNT_textLeft, kNT_textNormal);
    NT_drawText(50, 45, "degrees", 15, kNT_textLeft, kNT_textNormal);

    // Show delay time
    char delayStr[32];
    int delayMs = alg->v[kParamDelayTime];
    NT_intToString(delayStr, delayMs);
    NT_drawText(10, 60, delayStr, 15, kNT_textLeft, kNT_textNormal);
    NT_drawText(50, 60, "ms", 15, kNT_textLeft, kNT_textNormal);
    
    // Debug info
    if (!alg->delayBuffer) {
        NT_drawText(10, 55, "No DRAM", 15, kNT_textLeft, kNT_textNormal);
    }

    return false; // Don't suppress parameter display
}

float clamp(float value, float min, float max) {
    return std::min(std::max(value, min), max);
}

static const _NT_factory shiftDelayFactory = {
    NT_MULTICHAR('S', 'H', 'F', 'D'),
    "ShiftDelay",
    "Phase shift and time delay with pitch-adaptive control",
    0,
    nullptr,
    nullptr,
    nullptr,
    calculateRequirementsShiftDelay,
    constructShiftDelay,
    nullptr,
    stepShiftDelay,
    drawShiftDelay,
    nullptr,
    nullptr,
    kNT_tagEffect | kNT_tagDelay,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

extern "C" {
    uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
        switch (selector) {
        case kNT_selector_version:
            return kNT_apiVersionCurrent;
        case kNT_selector_numFactories:
            return 1;
        case kNT_selector_factoryInfo:
            if (data == 0)
                return reinterpret_cast<uintptr_t>(&shiftDelayFactory);
            return 0;
        default:
            return 0;
        }
    }
}

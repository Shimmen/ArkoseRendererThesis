#pragma once

#include "common.h"

#include <random>

class Random {
public:
    Random();
    explicit Random(unsigned int seed);

    // NOTE: Only use on the main thread!
    static Random& instance();

    float random();
    float randomBilateral();
    int randomInt(int minVal, int maxVal);

    float3 randomInXyUnitDisk();
    float3 randomInUnitSphere();

private:
    std::default_random_engine m_engine;
    std::uniform_real_distribution<float> m_uniformDistro { 0.0f, 1.0f };
    std::uniform_real_distribution<float> m_uniformBilateralDistro { -1.0f, 1.0f };
};

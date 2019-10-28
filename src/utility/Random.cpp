#include "Random.h"

#include <chrono>

Random::Random()
    : m_engine(std::chrono::high_resolution_clock::now().time_since_epoch().count())
{
}

Random::Random(unsigned int seed)
    : m_engine(seed)
{
}

Random& Random::instance()
{
    static Random random {};
    return random;
}

float Random::random()
{
    return m_uniformDistro(m_engine);
}

float Random::randomBilateral()
{
    return m_uniformBilateralDistro(m_engine);
}

int Random::randomInt(int minVal, int maxVal)
{
    std::uniform_int_distribution<> distribution(minVal, maxVal);
    return distribution(m_engine);
}

float3 Random::randomInXyUnitDisk()
{
    float3 position {};
    do {
        position = float3(randomBilateral(), randomBilateral(), 0.0f);
    } while (length2(position) >= 1.0f);
    return position;
}

float3 Random::randomInUnitSphere()
{
    float3 position {};
    do {
        position = float3(randomBilateral(), randomBilateral(), randomBilateral());
    } while (linalg::length2(position) >= 1.0f);
    return position;
}

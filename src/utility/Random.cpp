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

vec3 Random::randomInXyUnitDisk()
{
    vec3 position {};
    do {
        position = vec3(randomBilateral(), randomBilateral(), 0.0f);
    } while (mathkit::length2(position) >= 1.0f);
    return position;
}

glm::vec3 Random::randomInUnitSphere()
{
    vec3 position {};
    do {
        position = vec3(randomBilateral(), randomBilateral(), randomBilateral());
    } while (mathkit::length2(position) >= 1.0f);
    return position;
}

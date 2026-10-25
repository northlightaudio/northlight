#pragma once

#include <algorithm>
#include <cmath>

// Levels as decibels and as gains: one implementation for every module (no JUCE,
// so the processors, the engine and the views share it). Each caller says where
// silence starts, its floor: a gain at or below the floor's is 0, and silence
// reads as the floor in dB.
//   processorFloorDb   (-120) the built-in processors
//   measurementFloorDb (-200) the export's loudness and peak figures
//   ui::db::silence    (-100) what the views show (ui/Db.h)
//   magnitudeFloorDb   (-180) a filter's or a spectrum's magnitude response
namespace northlight::engine
{

inline constexpr double processorFloorDb = -120.0;
inline constexpr double measurementFloorDb = -200.0;
inline constexpr double magnitudeFloorDb = -180.0;

inline float dbToGain(float db, float floorDb) noexcept
{
    return db <= floorDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}
inline double dbToGain(double db, double floorDb) noexcept
{
    return db <= floorDb ? 0.0 : std::pow(10.0, db / 20.0);
}

inline float gainToDb(float gain, float floorDb) noexcept
{
    return gain > 0.0f ? std::max(floorDb, 20.0f * std::log10(gain)) : floorDb;
}
inline double gainToDb(double gain, double floorDb) noexcept
{
    return gain > 0.0 ? std::max(floorDb, 20.0 * std::log10(gain)) : floorDb;
}

} // namespace northlight::engine

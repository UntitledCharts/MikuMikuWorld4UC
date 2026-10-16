#pragma once
#include "Constants.h"
#include "ScoreEvents.h"
#include <map>
#include <unordered_map>
#include <vector>

namespace MikuMikuWorld
{
	qnote_t quatersPerMeasure(const TimeSignature& t);
	qnote_t quartersPerBeat(const TimeSignature& t);
	beat_t beatsPerMeasure(const TimeSignature& t);

	qnote_t ticksToQuarters(tick_t ticks, tick_t quarterTicks = TICKS_PER_QUARTER);
	tick_t quartersToTicks(qnote_t quarters, tick_t quarterTicks = TICKS_PER_QUARTER);

	secs_t quartersToSecs(qnote_t quarters, float minuteQuarters);
	qnote_t secsToQuarters(secs_t secs, float minuteQuarters);

	secs_t accumulateDuration(tick_t tick, const TempoCollection& tempos,
	                          tick_t quarterTicks = TICKS_PER_QUARTER);
	tick_t accumulateTicks(secs_t secs, const TempoCollection& tempos,
	                       tick_t quarterTicks = TICKS_PER_QUARTER);

	measure_t accumulateMeasures(tick_t ticks, const TimeSignatureCollection& ts,
	                             tick_t quarterTicks = TICKS_PER_QUARTER);
	tick_t accumulateTicks(measure_t measure, const TimeSignatureCollection& ts,
	                       tick_t quarterTicks = TICKS_PER_QUARTER);

	beat_t accumulateBeats(measure_t measure, const TimeSignatureCollection& ts);

	struct Score;
	TimeSignature& getTimeSignAt(Score& score, measure_t measure);
	Tempo& getTempoAt(Score& score, tick_t tick);

	float getEasedSpeed(tick_t startTick, tick_t endTick, float startSpeed, float endSpeed,
	                    tick_t currentTick, HiSpeedEaseType ease);
	float getScaledTick(tick_t startTick, tick_t endTick, float startSpeed, float endSpeed,
	                    HiSpeedEaseType ease, tick_t fromTick = -1, tick_t toTick = -1);
	float accumulateScaledTick(tick_t tick, id_t layer, const HiSpeedCollection& hs,
	                           const HiSpeedOrderedCollection* ordHs = nullptr, tick_t fromTick = 0);
}

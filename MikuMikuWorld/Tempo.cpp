#include "Tempo.h"
#include "Constants.h"
#include "Score.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace MikuMikuWorld
{
	float quatersPerMeasure(const TimeSignature& t) { return 4.0f * t.numerator / t.denominator; }
	qnote_t quartersPerBeat(const TimeSignature& t)
	{
		return quatersPerMeasure(t) / beatsPerMeasure(t);
	}
	beat_t beatsPerMeasure(const TimeSignature& t) { return t.numerator; }

	qnote_t ticksToQuarters(tick_t ticks, tick_t quarterTicks)
	{
		return static_cast<qnote_t>(ticks) / quarterTicks;
	}

	tick_t quartersToTicks(qnote_t quarters, tick_t quarterTicks)
	{
		static_assert(sizeof(long) == sizeof(tick_t));
		return std::lround(quarters * quarterTicks);
	}

	secs_t quartersToSecs(qnote_t quarters, float minuteQuarters)
	{
		return quarters * 60.f / minuteQuarters;
	}

	qnote_t secsToQuarters(secs_t secs, float minuteQuarters)
	{
		return secs * minuteQuarters / 60.f;
	}

	secs_t accumulateDuration(tick_t tick, const TempoCollection& tempos, tick_t quarterTicks)
	{
		if (tempos.empty())
			return 0;
		secs_t total = 0;

		auto last = tempos.upper_bound(tick);
		if (last == tempos.begin()) // probably negative tick
			++last;
		for (auto pv = tempos.begin(), it = std::next(pv); it != last; pv = it, ++it)
		{
			auto&& [prevTick, prevTempo] = *pv;
			auto&& [currTick, currTempo] = *it;
			total += quartersToSecs(ticksToQuarters(currTick - prevTick, quarterTicks),
			                        prevTempo.quarterPerMinute);
		}

		auto&& [lastTick, lastTempo] = *std::prev(last);
		total += quartersToSecs(ticksToQuarters(tick - lastTick, quarterTicks),
		                        lastTempo.quarterPerMinute);

		return total;
	}

	tick_t accumulateTicks(secs_t secs, const TempoCollection& tempos, tick_t quarterTicks)
	{
		if (tempos.empty())
			return 0;
		tick_t total = 0;
		secs_t accSecs = 0;

		auto pv = tempos.begin();
		for (auto it = std::next(pv); it != tempos.end(); pv = it, ++it)
		{
			auto&& [prevTick, prevTempo] = *pv;
			auto&& [currTick, currTempo] = *it;

			tick_t ticks = currTick - prevTick;
			float seconds =
			    quartersToSecs(ticksToQuarters(ticks, quarterTicks), prevTempo.quarterPerMinute);

			if (accSecs + seconds >= secs)
				break;
			total += ticks;
			accSecs += seconds;
		}

		total += quartersToTicks(secsToQuarters(secs - accSecs, pv->second.quarterPerMinute),
		                         quarterTicks);

		return total;
	}

	measure_t accumulateMeasures(tick_t tick, const TimeSignatureCollection& ts,
	                             tick_t quarterTicks)
	{
		if (ts.empty())
			return 0;
		const qnote_t endQuarts = ticksToQuarters(tick, quarterTicks);
		qnote_t accQuarts = 0;
		measure_t total = 0;

		auto pv = ts.begin();
		for (auto it = std::next(pv); it != ts.end(); pv = it, ++it)
		{
			auto&& [prevMeasure, prevTimeSig] = *pv;
			auto&& [currMeasure, currTimeSig] = *it;

			measure_t measures = currMeasure - prevMeasure;
			qnote_t quarters = measures * quatersPerMeasure(prevTimeSig);

			if ((accQuarts + quarters) >= endQuarts)
				break;
			total += measures;
			accQuarts += quarters;
		}

		total += (endQuarts - accQuarts) / quatersPerMeasure(pv->second);

		return total;
	}

	tick_t accumulateTicks(measure_t measure, const TimeSignatureCollection& ts,
	                       tick_t quarterTicks)
	{
		if (ts.empty())
			return 0;
		tick_t total = 0;

		auto last = ts.upper_bound(measure);
		for (auto pv = ts.begin(), it = std::next(pv); it != last; pv = it, ++it)
		{
			auto&& [prevMeasure, prevTimeSig] = *pv;
			auto&& [currMeasure, currTimeSig] = *it;

			measure_t measures = currMeasure - prevMeasure;
			qnote_t quarters = measures * quatersPerMeasure(prevTimeSig);

			total += quartersToTicks(quarters, quarterTicks);
		}

		auto&& [prevMeasure, prevTimeSig] = *std::prev(last);
		total +=
		    quartersToTicks((measure - prevMeasure) * quatersPerMeasure(prevTimeSig), quarterTicks);

		return total;
	}

	beat_t accumulateBeats(measure_t measure, const TimeSignatureCollection& ts)
	{
		if (ts.empty())
			return 0;
		float total = 0;

		auto last = ts.upper_bound(measure);
		for (auto pv = ts.begin(), it = std::next(pv); it != last; pv = it, ++it)
		{
			auto&& [prevMeasure, prevTimeSig] = *pv;
			auto&& [currMeasure, currTimeSig] = *it;

			measure_t measures = currMeasure - prevMeasure;
			total += measures * beatsPerMeasure(prevTimeSig);
		}

		auto&& [prevMeasure, prevTimeSig] = *std::prev(last);
		total += (measure - prevMeasure) * beatsPerMeasure(prevTimeSig);

		return total;
	}

	TimeSignature& getTimeSignAt(Score& score, measure_t measure)
	{
		return std::prev(score.timeSignatures.upper_bound(measure))->second;
	}

	Tempo& getTempoAt(Score& score, tick_t tick)
	{
		return std::prev(score.tempoChanges.upper_bound(tick))->second;
	}

	float getEasedSpeed(tick_t startTick, tick_t endTick, float startSpeed, float endSpeed,
	                    tick_t currentTick, HiSpeedEaseType ease)
	{
		switch (ease)
		{
		case HiSpeedEaseType::None:
			return startSpeed;
		case HiSpeedEaseType::Linear:
			return lerp(startSpeed, endSpeed, unlerp(startTick, endTick, currentTick));
		default:
			assert(false && "EaseType not supported!");
			return 0.0f;
		}
	}

	float getScaledTick(tick_t startTick, tick_t endTick, float startSpeed, float endSpeed,
	                    HiSpeedEaseType ease, tick_t fromTick, tick_t toTick)
	{
		if (fromTick < 0)
			fromTick = startTick;
		if (toTick < 0)
			toTick = endTick;
		if (endTick < startTick || fromTick < startTick || toTick > endTick)
			throw std::out_of_range("Argument out of range!");
		switch (ease)
		{
		case HiSpeedEaseType::None:
			return (toTick - fromTick) * startSpeed;
		case HiSpeedEaseType::Linear:
		{
			// Solve for ax+b
			float a = (endSpeed - startSpeed) / (endTick - startTick);
			float b = endSpeed - endTick * a;
			// Substitute for integral
			auto f_int = [=](float x) { return a / 2 * std::pow(x, 2) + b * x; };
			return f_int(toTick) - f_int(fromTick);
		}
		default:
			assert(false && "EaseType not supported!");
			return 0.0f;
		}
	}

	template <typename hs_t>
	static float impl_accumulateScaledTick(tick_t tick, id_t layer, const HiSpeedCollection& hs,
	                                       std::multimap<layered_tick_t, hs_t*> ordHs,
	                                       tick_t fromTick)
	{
		float totalTicks = 0.0f;
		tick_t lastTick = 0, baseTick = fromTick;
		float lastSpeed = 1.0f;
		HiSpeedEaseType ease = HiSpeedEaseType::None;
		auto it = ordHs.upper_bound({ layer, fromTick }), end = ordHs.lower_bound({ layer, tick });
		if (it != ordHs.begin())
		{
			const HiSpeed& currHspd = *std::prev(it)->second;
			ease = currHspd.ease;
			lastTick = currHspd.tick;
			lastSpeed = currHspd.speed;
		}
		for (; it != end; ++it)
		{
			const HiSpeed& nextHspd = *it->second;
			if (lastTick < nextHspd.tick)
			{
				totalTicks += getScaledTick(lastTick, nextHspd.tick, lastSpeed, nextHspd.speed,
				                            ease, baseTick);
			}

			baseTick = lastTick = nextHspd.tick;
			lastSpeed = nextHspd.speed;
			ease = nextHspd.ease;
		}
		if (it != ordHs.end() && it->first.first == layer)
		{
			totalTicks += getScaledTick(lastTick, it->second->tick, lastSpeed, it->second->speed,
			                            ease, baseTick, tick);
		}
		else
		{
			// No next hispeed in the current layer, no ease
			totalTicks +=
			    getScaledTick(lastTick, tick, lastSpeed, -1, HiSpeedEaseType::None, baseTick);
		}
		return totalTicks;
	}

	float accumulateScaledTick(tick_t tick, id_t layer, const HiSpeedCollection& hs,
	                           const HiSpeedOrderedCollection* ordHs, tick_t fromTick)
	{
		if (ordHs)
			return impl_accumulateScaledTick(tick, layer, hs, *ordHs, fromTick);

		std::multimap<layered_tick_t, const HiSpeed*> ownedHs;
		for (auto& [_, hispeed] : hs)
		{
			if (hispeed.layer != layer)
				// No need to allocate for other layers
				continue;
			ownedHs.emplace(layered_tick_t{ hispeed.layer, hispeed.tick }, &hispeed);
		}
		return impl_accumulateScaledTick(tick, layer, hs, ownedHs, fromTick);
	}

	bool isSame(const HiSpeed& a, const HiSpeed& b)
	{
		return a.tick == b.tick && a.layer == b.layer && a.speed == b.speed && a.ease == b.ease &&
		       a.skips == b.skips && a.hideNotes == b.hideNotes;
	}
}

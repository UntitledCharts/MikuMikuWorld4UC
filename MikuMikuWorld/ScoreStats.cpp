#include "ScoreStats.h"
#include "Score.h"
#include <functional>
#include <algorithm>
#include <numeric>

namespace MikuMikuWorld
{
	static bool isNormalTapNote(const Note& note)
	{
		return note.type == NoteType::Tap && !note.isFlick() && !note.isTrace() && !note.isHidden();
	}

	template <typename T, typename = void> struct has_second : std::false_type
	{
	};
	template <typename T>
	struct has_second<T, std::void_t<decltype(std::declval<T>().second)>> : std::true_type
	{
	};
	template <typename TContainer, typename TFunc> static int getCount(const TContainer& m, TFunc f)
	{
		auto count_f = [&](const auto& e)
		{
			if constexpr (has_second<decltype(e)>::value)
				return std::invoke(f, e.second);
			else
				return std::invoke(f, e);
		};
		return std::count_if(std::begin(m), std::end(m), count_f);
	}

	static int getNormalHoldNoteCount(const HoldNote& hold)
	{
		auto isNormalHoldStep = [](const HoldNoteStep& step)
		{
			return !hasFlag(step.flag, HoldNoteFlag::Guide) &&
			       !hasFlag(step.flag, HoldNoteFlag::Dummy);
		};
		return getCount(hold.separators, isNormalHoldStep);
	}

	static int getGuideHoldNoteCount(const HoldNote& hold)
	{
		return getCount(hold.separators, [](const HoldNoteStep& step) { return step.isGuide(); });
	}

	template <typename TMapContainer, typename TFunc>
	static int getTotal(const TMapContainer& m, TFunc f)
	{
		return std::accumulate(std::begin(m), std::end(m), 0,
		                       [&](int count, const TMapContainer::value_type& e)
		                       { return count + std::invoke(f, e.second); });
	}

	ScoreStats::ScoreStats() { reset(); }

	void ScoreStats::reset()
	{
		taps = flicks = holds = guides = ticks = traces = damages = dummies = total = combo = 0;
		hispeeds = 1;
	}

	void ScoreStats::calculateStats(const Score& score)
	{
		taps = getCount(score.notes, isNormalTapNote);

		holds = getTotal(score.holdNotes, getNormalHoldNoteCount);

		ticks = getCount(score.notes,
		                 [](const Note& n) { return n.type == NoteType::Tick && !n.isHidden(); });

		guides = getTotal(score.holdNotes, getGuideHoldNoteCount);

		flicks = getCount(score.notes, &Note::isFlick);

		traces = getCount(score.notes, &Note::isTrace);

		damages = getCount(score.notes, [](const Note& n)
		                   { return n.type == NoteType::Damage && !n.isHidden(); });

		dummies =
		    getCount(score.notes, [](const Note& n) { return hasFlag(n.flag, NoteFlag::Dummy); });

		hispeeds = score.hiSpeedChanges.size();

		total = score.notes.size();
		calculateCombo(score);
	}

	void ScoreStats::calculateCombo(const Score& score)
	{
		combo = score.notes.size();

		combo -= getCount(score.notes, [](const Note& n) { return n.isHidden() || n.isDummy(); });

		constexpr int eighthTicks = TICKS_PER_QUARTER / 2;
		for (const auto& [id, hold] : score.holdNotes)
		{
			for (auto stepIt = hold.separators.begin(), endIt = std::next(stepIt); stepIt != endIt;)
			{
				const HoldNoteStep& holdStep = *stepIt;
				if (holdStep.isGuide() || holdStep.isDummy())
				{
					stepIt = endIt;
					if (endIt != hold.separators.end())
						++endIt;
					continue;
				}
				if (endIt != hold.separators.end() && (!endIt->isGuide() && !endIt->isDummy()))
				{
					++endIt;
					continue;
				}
				id_t endID = endIt != hold.separators.end() ? endIt->ID : hold.steps.back();

				int startTick = score.notes.at(stepIt->ID).tick + eighthTicks;
				if (startTick % eighthTicks)
					startTick -= (startTick % eighthTicks);
				int endTick = score.notes.at(endID).tick;
				if (endTick % eighthTicks)
					endTick += eighthTicks - (endTick % eighthTicks);

				stepIt = endIt;
				if (startTick >= endTick)
					continue;

				combo += (endTick - startTick) / eighthTicks;
			}
		}
	}
}

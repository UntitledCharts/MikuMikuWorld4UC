#pragma once
#include <set>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include "Constants.h"
#include "NoteTypes.h"

namespace MikuMikuWorld
{
	struct TimeSignature
	{
		measure_t measure = 0;
		int numerator = 4;
		int denominator = 4;
	};
	using TimeSignatureCollection = std::map<measure_t, TimeSignature>;

	struct Tempo
	{
		tick_t tick = 0;
		float quarterPerMinute = 60;
	};
	using TempoCollection = std::map<tick_t, Tempo>;

	enum class SkillEffect : uint8_t
	{
		Score,
		Heal,
		Perfect,
		EffectCount
	};

	struct Skill
	{
		tick_t tick;
		SkillEffect effect = SkillEffect::Score;
		uint8_t level = 1;
	};

	struct SkillCompare
	{
		inline constexpr bool operator()(const Skill& s1, const Skill& s2) const
		{
			return s1.tick < s2.tick;
		}
	};

	using SkillTriggerCollection = std::set<Skill, SkillCompare>;

	struct Fever
	{
		tick_t startTick = -1;
		tick_t endTick = -1;
	};

	struct HiSpeed
	{
		tick_t tick = 0;
		id_t layer = 0;
		id_t ID = -1;
		float speed = 1.0f;
		float skips = 0.0f;
		HiSpeedEaseType ease = HiSpeedEaseType::None;
		bool hideNotes = false;
	};
	using HiSpeedCollection = std::unordered_map<id_t, HiSpeed>;
	using HiSpeedRefCollection = std::unordered_set<id_t>;
	using HiSpeedOrderedCollection = std::multimap<layered_tick_t, HiSpeed*>;

	// This is purely a helper and should not be use as a comparator
	bool isSame(const HiSpeed&, const HiSpeed&);
}
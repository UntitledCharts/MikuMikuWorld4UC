#pragma once
#include "Constants.h"
#include "NoteTypes.h"
#include "Math.h"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <algorithm>

namespace MikuMikuWorld
{
	constexpr std::string_view SE_TAP = "tap";
	constexpr std::string_view SE_FLICK = "flick";
	constexpr std::string_view SE_TICK = "tick";
	constexpr std::string_view SE_FRICTION = "friction";
	constexpr std::string_view SE_LONG_TAP = "long_tap";
	constexpr std::string_view SE_CONNECT = "connect";
	constexpr std::string_view SE_CRITICAL_TAP = "critical_tap";
	constexpr std::string_view SE_CRITICAL_FLICK = "critical_flick";
	constexpr std::string_view SE_CRITICAL_TICK = "critical_tick";
	constexpr std::string_view SE_CRITICAL_FRICTION = "critical_friction";
	constexpr std::string_view SE_CRITICAL_LONG_TAP = "critical_long_tap";
	constexpr std::string_view SE_CRITICAL_CONNECT = "critical_connect";
	constexpr std::string_view SE_DAMAGE = "damage";

	constexpr std::string_view SE_NAMES[] = { SE_TAP,
		                                      SE_FLICK,
		                                      SE_TICK,
		                                      SE_FRICTION,
		                                      SE_LONG_TAP,
		                                      SE_CONNECT,
		                                      SE_CRITICAL_TAP,
		                                      SE_CRITICAL_FLICK,
		                                      SE_CRITICAL_TICK,
		                                      SE_CRITICAL_FRICTION,
		                                      SE_CRITICAL_LONG_TAP,
		                                      SE_CRITICAL_CONNECT,
		                                      SE_DAMAGE };

	struct Note
	{
		tick_t tick = 0;
		id_t layer = 0;
		id_t ID = -1;
		id_t holdID = -1;
		NoteType type{ NoteType::Tap };
		NoteFlag flag{ NoteFlag::None };
		FlickType flick{ FlickType::None };
		EaseType ease{ EaseType::Linear };
		SoundEffectType soundEffect{ SoundEffectType::Default };

		float lane = 0.0f;
		float width = 3.0f;
		float guideAlpha = 1.0f;

		constexpr inline bool canFlick() const { return type == NoteType::Tap && !isHidden(); }
		constexpr inline bool canTrace() const { return type == NoteType::Tap && !isHidden(); }
		constexpr inline bool canCrit() const
		{
			return type == NoteType::Tap || type == NoteType::Tick;
		}
		constexpr inline bool canDummy() const { return !isHidden(); }
		constexpr inline bool canSoundEffect() const { return !isDummy(); }
		constexpr inline bool canSimLine() const { return type != NoteType::Tick && !isHidden(); }

		constexpr inline bool isHold() const { return holdID != -1; }
		constexpr inline bool isFlick() const { return canFlick() && flick != FlickType::None; }
		constexpr inline bool isCrit() const
		{
			return canCrit() && hasFlag(flag, NoteFlag::Critical);
		}
		constexpr inline bool isTrace() const
		{
			return canTrace() && hasFlag(flag, NoteFlag::Trace);
		}
		constexpr inline bool isDummy() const
		{
			return canDummy() && hasFlag(flag, NoteFlag::Dummy);
		}
		constexpr inline bool isHidden() const { return hasFlag(flag, NoteFlag::Hidden); }
		constexpr inline bool isAttached() const
		{
			return isHold() && !hasFlag(flag, NoteFlag::NonAttached) &&
			       hasFlag(flag, NoteFlag::Attached);
		}
		constexpr inline bool hasEase() const { return isHold() && !isAttached(); }
	};

	using NoteCollection = std::unordered_map<id_t, Note>;
	using NoteOrderedCollection = std::multimap<tick_t, Note*>;
	using NoteRefCollection = std::unordered_set<id_t>;
	using NoteViewCollection = std::unordered_map<id_t, Note*>;
	struct NotesContext; // Forward declaration

	struct HoldNoteStep
	{
		id_t ID{ -1 }; // note id of the step
		HoldNoteFlag flag{ HoldNoteFlag::Normal };
		GuideColor guideColor{ GuideColor::Green };
		HoldStepLayer layer{ HoldStepLayer::Top };

	  public:
		constexpr inline bool isCrit() const { return hasFlag(flag, HoldNoteFlag::Critical); }
		constexpr inline bool isGuide() const { return hasFlag(flag, HoldNoteFlag::Guide); }
		constexpr inline bool isDummy() const { return hasFlag(flag, HoldNoteFlag::Dummy); }
	};

	class HoldNote
	{
	  public:
		id_t ID;
		FadeType fadeType{ FadeType::Out };
		// Steps are all the notes in the holds
		// A hold must always has atleast 2 steps (Begin, End)
		// These must always be ordered by the sort predicate
		std::vector<id_t> steps;
		// Seperators are steps that change the hold note look and function
		// HoldNote always has at minimum 1 separator
		std::vector<HoldNoteStep> separators;
		// Joints are steps that isn't a attached to the hold
		// The first and last step of a hold must be a joint
		// These are generate from note data and thus must be kept in sync with them
		std::vector<id_t> joints;

		template <typename C = std::less<>> struct StepComparer
		{
			constexpr bool operator()(const Note& n1, const Note& n2) const noexcept
			{
				C c{};
				return c(n1.tick, n2.tick) || c(n2.tick, n1.tick) ? c(n1.tick, n2.tick)
				                                                  : c(n1.lane, n2.lane);
			}
			constexpr bool operator()(const Note* n1, const Note* n2) const noexcept
			{
				return operator()(*n1, *n2);
			}
		};

		template <typename C = std::less<>> struct StepIdComparer : public StepComparer<C>
		{
			const NoteCollection& notes;
			StepIdComparer(const NoteCollection& n) : notes(n) {}
			using StepComparer<C>::operator();
			constexpr bool operator()(id_t s1, id_t s2) const noexcept
			{
				return operator()(notes.at(s1), notes.at(s2));
			}
			constexpr bool operator()(id_t s1, const Note& n2) const noexcept
			{
				return operator()(notes.at(s1), n2);
			}
			constexpr bool operator()(const Note& n1, id_t s2) const noexcept
			{
				return operator()(n1, notes.at(s2));
			}
		};

		template <typename C = std::less<>> struct HoldStepComparer : public StepIdComparer<C>
		{
			HoldStepComparer(const NoteCollection& n) : StepIdComparer<C>(n) {}
			using StepIdComparer<C>::operator();
			constexpr bool operator()(const HoldNoteStep& s1, const HoldNoteStep& s2) const noexcept
			{
				return operator()(s1.ID, s2.ID);
			}
			constexpr bool operator()(const HoldNoteStep& s1, id_t s2) const noexcept
			{
				return operator()(s1.ID, s2);
			}
			constexpr bool operator()(id_t s1, const HoldNoteStep& s2) const noexcept
			{
				return operator()(s1, s2.ID);
			}
			constexpr bool operator()(const HoldNoteStep& s1, const Note& n2) const noexcept
			{
				return operator()(s1.ID, n2);
			}
			constexpr bool operator()(const Note& n1, const HoldNoteStep& s2) const noexcept
			{
				return operator()(n1, s2.ID);
			}
		};

		// Can be affected when guideAlpha change
		bool canGuideAlpha(const Note& step, const NoteCollection& notes) const;
		bool canSetGuideAlpha(const Note& step, const NoteCollection& notes) const;

		void insertStep(Note& note, NoteCollection& notes, bool swaps = false, bool update = true);
		void sortSteps(NoteCollection& notes, bool swaps = false);
		void updateSeparators(NoteCollection& notes);
		void updateJoints(NoteCollection& notes);
		void updateLongs(NoteCollection& notes);
		void updateFading(NoteCollection& notes);

		// Find the first joint not less than the step
		const Note* jointBeforeStep(const Note& step, const NoteCollection& notes) const;
		// Find the last joint not greater than the step
		const Note* jointAfterStep(const Note& step, const NoteCollection& notes) const;

		HoldNoteStep& holdStepAt(const Note& step, const NoteCollection& notes);
		const HoldNoteStep& holdStepAt(const Note& step, const NoteCollection& notes) const;
	};
	using HoldNoteCollection = std::unordered_map<id_t, HoldNote>;

	struct NotesContext
	{
		NoteCollection notes;
		HoldNoteCollection holdNotes;
	};

	void setNotePosition(Note& n1, const Note& n2);
	void swapNotePosition(Note& n1, Note& n2);
	void swapNoteProperties(Note& n1, Note& n2);
	std::string_view getNoteSE(const Note& note);
}

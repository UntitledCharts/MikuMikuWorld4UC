#include "Note.h"
#include "Constants.h"
#include "Score.h"
#include "ApplicationConfiguration.h"
#include <algorithm>

namespace MikuMikuWorld
{
	bool isSame(const Note& a, const Note& b)
	{
		return a.tick == b.tick && a.layer == b.layer && a.type == b.type && a.flag == b.flag &&
		       a.flick == b.flick && a.ease == b.ease && a.soundEffect == b.soundEffect &&
		       a.lane == b.lane && a.width == b.width && a.guideAlpha == b.guideAlpha;
	}

	bool isSame(const HoldNoteStep& a, const HoldNoteStep& b)
	{
		return a.flag == b.flag && a.guideColor == b.guideColor && a.layer == b.layer;
	}

	static std::vector<HoldNoteStep>::iterator holdStepIteratorAt(const Note& step,
	                                                              std::vector<HoldNoteStep>& steps,
	                                                              const NoteCollection& notes)
	{
		return std::prev(
		    std::upper_bound(steps.begin(), steps.end(), step, HoldNote::HoldStepComparer(notes)));
	}

	static std::vector<HoldNoteStep>::const_iterator
	holdStepIteratorAt(const Note& step, const std::vector<HoldNoteStep>& steps,
	                   const NoteCollection& notes)
	{
		return std::prev(
		    std::upper_bound(steps.begin(), steps.end(), step, HoldNote::HoldStepComparer(notes)));
	}

	bool HoldNote::canGuideAlpha(const Note& step, const NoteCollection& notes) const
	{
		auto it = holdStepIteratorAt(step, separators, notes);
		return it->isGuide() || (it != separators.begin() && std::prev(it)->isGuide());
	}

	bool HoldNote::canSetGuideAlpha(const Note& step, const NoteCollection& notes) const
	{
		// Only if fade type is custom
		if (fadeType != FadeType::Custom)
			return false;
		auto it = holdStepIteratorAt(step, separators, notes);
		// Only separator notes can have custom alpha
		if (step.ID != it->ID && step.ID != steps.back())
			return false;
		return it->isGuide() || (it != separators.begin() && std::prev(it)->isGuide());
	}

	void HoldNote::insertStep(Note& note, NoteCollection& notes, bool swaps, bool update)
	{
		note.holdID = ID;
		auto it = std::upper_bound(steps.begin(), steps.end(), note, StepIdComparer(notes));
		if (it == steps.begin())
		{
			Note& prevStart = notes.at(steps.front());
			if (swaps)
				swapNoteProperties(prevStart, note);
			else
			{
				prevStart.flag =
				    setFlag(prevStart.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);
				note.flag = setFlag(note.flag, NoteFlag::LongNote);
				if (hasFlag(note.flag, NoteFlag::Attached))
					note.flag = setFlag(note.flag, NoteFlag::NonAttached);
			}
			steps.insert(it, note.ID);
		}
		else if (it == steps.end())
		{
			Note& prevEnd = notes.at(steps.back());
			if (swaps)
				swapNoteProperties(prevEnd, note);
			else
			{
				prevEnd.flag =
				    setFlag(prevEnd.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);
				note.flag = setFlag(note.flag, NoteFlag::LongNote);
				if (hasFlag(note.flag, NoteFlag::Attached))
					note.flag = setFlag(note.flag, NoteFlag::NonAttached);
			}
			steps.insert(it, note.ID);
		}
		else
		{
			steps.insert(it, note.ID);
			note.flag = setFlag(note.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);
		}
		if (update)
		{
			updateJoints(notes);
			updateFading(notes);
		}
	}

	void HoldNote::sortSteps(NoteCollection& notes, bool swaps)
	{
		Note& prevStart = notes.at(steps.front());
		Note& prevEnd = notes.at(steps.back());
		prevStart.flag = setFlag(prevStart.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);
		prevEnd.flag = setFlag(prevEnd.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);

		std::stable_sort(steps.begin(), steps.end(), StepIdComparer(notes));
		Note& start = notes.at(steps.front());
		Note& end = notes.at(steps.back());
		if (swaps)
		{
			if (prevStart.ID != start.ID)
				swapNoteProperties(prevStart, start);
			if (prevEnd.ID != end.ID)
				swapNoteProperties(prevEnd, end);
		}
		if (hasFlag(start.flag, NoteFlag::Attached))
			start.flag = setFlag(start.flag, NoteFlag::NonAttached);
		if (hasFlag(end.flag, NoteFlag::Attached))
			end.flag = setFlag(end.flag, NoteFlag::NonAttached);

		updateSeparators(notes);
		updateLongs(notes);
		updateJoints(notes);
		updateFading(notes);
	}

	void HoldNote::updateSeparators(NoteCollection& notes)
	{
		separators.front().ID = steps.front();
		std::sort(std::next(separators.begin()), separators.end(), HoldStepComparer(notes));
	}

	void HoldNote::updateJoints(NoteCollection& notes)
	{
		joints.clear();
		for (size_t i = 0; i < steps.size(); ++i)
			if (!notes.at(steps[i]).isAttached())
				joints.push_back(steps[i]);
	}

	void HoldNote::updateLongs(NoteCollection& notes)
	{
		HoldNoteStep* prevStep = &separators.front();
		for (auto it = std::next(separators.begin()); it != separators.end(); ++it)
		{
			HoldNoteStep& holdStep = *it;
			Note& step = notes.at(holdStep.ID);
			step.flag = setFlag(step.flag, NoteFlag::LongNote,
			                    holdStep.isGuide() != prevStep->isGuide() || step.isHidden());
			prevStep = &holdStep;
		}
		Note& start = notes.at(steps.front());
		Note& end = notes.at(steps.back());
		start.flag = setFlag(start.flag, NoteFlag::LongNote,
		                     !separators.front().isGuide() || start.isHidden());
		end.flag = setFlag(end.flag, NoteFlag::LongNote, !prevStep->isGuide() || end.isHidden());
	}

	void HoldNote::updateFading(NoteCollection& notes)
	{
		static_assert(size_t(FadeType::FadeTypeCount) == 5 && "Update this");
		Note& startStep = notes.at(steps.front());
		Note& endStep = notes.at(steps.back());
		size_t endIdx = steps.size() - 1;
		auto nextSeparatorIt = separators.begin();
		const HoldNoteStep* holdStep = &*nextSeparatorIt;
		++nextSeparatorIt;
		Note* startHoldStep = &notes.at(holdStep->ID);
		Note* endHoldStep =
		    nextSeparatorIt != separators.end() ? &notes.at(nextSeparatorIt->ID) : &endStep;
		for (size_t idx = 0; idx <= endIdx; idx++)
		{
			bool wasGuide = holdStep->isGuide();
			if (nextSeparatorIt != separators.end() && nextSeparatorIt->ID == steps[idx])
			{
				holdStep = &(*nextSeparatorIt);
				++nextSeparatorIt;
				startHoldStep = endHoldStep;
				endHoldStep =
				    nextSeparatorIt != separators.end() ? &notes.at(nextSeparatorIt->ID) : &endStep;
			}
			if (!holdStep->isGuide() && !wasGuide)
				continue;

			Note& step = notes.at(steps[idx]);
			switch (fadeType)
			{
			case FadeType::Out:
				step.guideAlpha = unlerp(endStep.tick, startStep.tick, step.tick);
				break;
			case FadeType::None:
				step.guideAlpha = 1.f;
				break;
			case FadeType::In:
				step.guideAlpha = unlerp(startStep.tick, endStep.tick, step.tick);
				break;
			case FadeType::Classic:
				step.guideAlpha = lerp(1.0f, 0.2f, float(idx) / endIdx);
				break;
			default:
			case FadeType::Custom:
			{
				if (step.ID == startHoldStep->ID || step.ID == endHoldStep->ID)
					break;
				float ratio = unlerp(startHoldStep->tick, endHoldStep->tick, step.tick, 0.5f);
				step.guideAlpha = lerp(startHoldStep->guideAlpha, endHoldStep->guideAlpha, ratio);
				break;
			}
			}
		}
	}

	const Note* HoldNote::jointBeforeStep(const Note& step, const NoteCollection& notes) const
	{
		auto cmp = StepIdComparer<std::less<>>(notes);
		auto it = std::lower_bound(joints.begin(), joints.end(), step, cmp);
		if (it == joints.end())
			return &notes.at(*std::prev(it));
		if (cmp(step, *it))
			return it != joints.begin() ? &notes.at(*std::prev(it)) : nullptr;
		else
			return &notes.at(*it);
	}

	const Note* HoldNote::jointAfterStep(const Note& step, const NoteCollection& notes) const
	{
		auto cmp = StepIdComparer<std::greater<>>(notes);
		auto it = std::lower_bound(joints.rbegin(), joints.rend(), step, cmp);
		if (it == joints.rend())
			return &notes.at(*std::prev(it));
		if (cmp(step, *it))
			return it != joints.rbegin() ? &notes.at(*std::prev(it)) : nullptr;
		else
			return &notes.at(*it);
	}

	HoldNoteStep& HoldNote::holdStepAt(const Note& step, const NoteCollection& notes)
	{
		return *holdStepIteratorAt(step, separators, notes);
	}

	const HoldNoteStep& HoldNote::holdStepAt(const Note& step, const NoteCollection& notes) const
	{
		return *holdStepIteratorAt(step, separators, notes);
	}

	void setNotePosition(Note& n1, const Note& n2)
	{
		n1.tick = n2.tick;
		n1.lane = n2.lane;
		n1.width = n2.width;
	}

	void swapNotePosition(Note& n1, Note& n2)
	{
		std::swap(n1.tick, n2.tick);
		std::swap(n1.lane, n2.lane);
		std::swap(n1.width, n2.width);
	}

	void swapNoteProperties(Note& n1, Note& n2)
	{
		std::swap(n1.type, n2.type);
		std::swap(n1.flag, n2.flag);
		std::swap(n1.flick, n2.flick);
		std::swap(n1.ease, n2.ease);
		std::swap(n1.guideAlpha, n2.guideAlpha);
	}

	std::string_view getNoteSE(const Note& note)
	{
		if (note.soundEffect != SoundEffectType::Default)
		{
			switch (note.soundEffect)
			{
			default:
			case SoundEffectType::None:
				return "";
			case SoundEffectType::TapPerfect:
				return SE_TAP;
			case SoundEffectType::Flick:
				return SE_FLICK;
			case SoundEffectType::Trace:
				return SE_FRICTION;
			case SoundEffectType::Tick:
				return SE_TICK;
			case SoundEffectType::CritTap:
				return SE_CRITICAL_TAP;
			case SoundEffectType::CritFlick:
				return SE_CRITICAL_FLICK;
			case SoundEffectType::CritTrace:
				return SE_CRITICAL_FRICTION;
			case SoundEffectType::CritTick:
				return SE_CRITICAL_TICK;
			case SoundEffectType::Damage:
				return SE_DAMAGE;
			}
		}
		if (note.isHidden())
			return "";
		switch (note.type)
		{
		case NoteType::Tap:
			if (note.isFlick())
				return note.isCrit() ? SE_CRITICAL_FLICK : SE_FLICK;
			else if (note.isTrace())
				return note.isCrit() ? SE_CRITICAL_FRICTION : SE_FRICTION;
			else if (note.isHold())
				return note.isCrit() ? SE_CRITICAL_LONG_TAP : SE_LONG_TAP;
			else
				return note.isCrit() ? SE_CRITICAL_TAP : SE_TAP;
		case NoteType::Tick:
			return note.isCrit() ? SE_CRITICAL_TICK : SE_TICK;
		case NoteType::Damage:
			return SE_DAMAGE;
		default:
			return "";
		}
	}
}

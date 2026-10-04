#include "ScoreContext.h"
#include "Constants.h"
#include "IO.h"
#include "UI.h"
#include "Utilities.h"
#include "Math.h"
#include "PlatformIO.h"
#include "ScoreEditorTimeline.h"
#include <cassert>
#include <cstdio>

using json = nlohmann::json;
using namespace IO;

/*
    NOTES: for ScoreContext
    Score and ScoreMetadata are not responsible for keeping the correctness of the score data
    It's the responsiblity of the functions that access/modify the score data to make sure the score
    remains correct when access/modify it (referably by going through ScoreContext)
    General correctness include:
    - Note:
        + Have width within minNoteWidth and maxNoteWidth
        + Have lane within minLane and maxLane(note.width)
        + Have holdID < 0 if note is not in a HoldNote
        + Not have flags that contradict eachother like NoteType::Tick & Friction
        + Have valid flick and ease type
        + Have 0 < guideAlpha < 1
    - HoldNote:
        + Have atleast 2 steps, and a start and end joint
        + Have steps and joints sorted in a given order
        + Start and end steps of a note chain must not be isAttached() == true
        + Not have flags that contradict eachother like Guide & Dummy

    NOTES: for ScoreMetadata::isExtendedScore
    This variable denotes whether the score can have extended features
    If isExtendedScore is false the following must be true
    - All notes that are not in a HoldNote:
        + Must be type NoteType::Tap
        + Must not be Hidden or Dummy (or Attached)
        + Layer = 0
        + FlickType && EaseType be valid and within support range
    - All hold notes:
        + Must not be Dummy
        + Must have any separators
        + First step and last Step must be NoteType::Tap and must not be Dummy (or Attached)
        + Every steps between first and last must be NoteType::Tick
        * For non Guide hold notes:
            + Crit must be the same as the first step
            + First step must have the FlickType::None
            + Every steps between first and last must have the same crit as the first
            + Last step must be of the same crit as the first
              unless the first step is not crit and the last step has friction or flick
            + At least 2 joints
        * For Guide hold notes:
            + All steps must be Hidden (and not Attached)
            + GuideColor can only be GuideColor::Green or GuideColor::Yellow
            + FadeType must be FadeType::Classic
            + Have steps = joints
    - All hispeed changes:
        + Must have skips = 0, ease = None, hideNotes = None
    - ScoreContext::selectedLayer must not change to anything other than 0
*/

namespace MikuMikuWorld
{
	constexpr const char* clipboardSignature = "MikuMikuWorld clipboard";

	static bool flip(bool v) { return !v; }
	static bool set(bool v) { return true; }
	static bool unset(bool v) { return false; }

	void EditArgs::changeInsertMode(InsertMode newMode)
	{
		if (insertMode != newMode)
		{
			insertMode = newMode;
		}
		else
		{
			switch (insertMode)
			{
			case InsertMode::InsertLong:
				easeType = cycleMode(easeType, EaseType::EaseTypeCount);
				break;
			case InsertMode::InsertLongMid:
				stepType = cycleMode(stepType, EditHoldStepType::HoldStepTypeCount);
				break;
			case InsertMode::InsertFlick:
				flickType = cycleMode(flickType, FlickType::FlickTypeCount);
				flickType = flickType != FlickType::None ? flickType : FlickType::Default;
				break;
			case InsertMode::InsertGuide:
				colorType = cycleMode(colorType, GuideColor::GuideColorCount);
				break;
			case InsertMode::InsertHiSpeed:
				hiSpeedHideNotes = !hiSpeedHideNotes;
				break;
			}
		}
	}

	bool EditArgs::isNoteInsertMode() const
	{
		static_assert(size_t(InsertMode::MakeDummy) + 1 == size_t(InsertMode::InsertBPM));
		return insertMode >= InsertMode::InsertTap && insertMode <= InsertMode::MakeDummy;
	}

	float ScoreContext::minNoteWidth() const noexcept { return metadata.isExtendedScore ? 0 : 1; }
	float ScoreContext::maxNoteWidth() const noexcept
	{
		return metadata.isExtendedScore ? 12 + metadata.laneExtension * 2 : 12;
	}

	float ScoreContext::maxNoteWidth(float lane) const noexcept
	{
		return (metadata.isExtendedScore ? 12 + metadata.laneExtension : 12) - lane;
	}

	float ScoreContext::minLane() const noexcept { return -metadata.laneExtension; }

	float ScoreContext::maxLane() const noexcept
	{
		return ScoreEditorTimeline::NUM_LANES + metadata.laneExtension;
	}

	float ScoreContext::maxLane(float width) const noexcept
	{
		return ScoreEditorTimeline::NUM_LANES + metadata.laneExtension - width;
	}

	EaseType ScoreContext::maxEase() const noexcept
	{
		return metadata.isExtendedScore ? EaseType::EaseTypeCount : EaseType::EaseInOut;
	}

	FlickType ScoreContext::maxFlick() const noexcept
	{
		return metadata.isExtendedScore ? FlickType::FlickTypeCount : FlickType::Down;
	}

	void ScoreContext::setStep(EditHoldStepType type)
	{
		if (selection.emptyNotes())
			return;

		bool edit = false;
		std::unordered_set<id_t> updatingHolds;
		for (auto&& [ID, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			const HoldNote* hold = note.isHold() ? &score.holdNotes.at(note.holdID) : nullptr;
			if (!metadata.isExtendedScore &&
			    (!hold || hold->holdStepAt(note, score.notes).isGuide()))
				continue;

			EditHoldStepType curType = type;
			// Skip note must belong in a hold and not the first or last step in a hold
			bool cannotSetSkip = !hold || ID == hold->steps.front() || ID == hold->steps.back();
			if (curType == EditHoldStepType::HoldStepTypeCount)
			{
				// Cycle Note -> Hidden -> Attached
				if (!note.isAttached())
				{
					if (!note.isHidden())
						curType = EditHoldStepType::Hidden;
					else if (cannotSetSkip)
						curType = EditHoldStepType::Normal;
					else
						curType = EditHoldStepType::Skip;
				}
				else
				{
					curType = EditHoldStepType::Normal;
				}
			}

			switch (curType)
			{
			case EditHoldStepType::Normal:
				edit |= note.isHidden();
				note.flag = setFlag(note.flag, NoteFlag::Hidden | NoteFlag::NonAttached, false);
				if (note.isAttached())
				{
					edit = true;
					note.flag = setFlag(note.flag, NoteFlag::Attached, false);
				}
				break;
			case EditHoldStepType::Hidden:
				note.flick = FlickType::None;
				note.flag = setFlag(note.flag, NoteFlag::Trace | NoteFlag::NonAttached, false);
				if (note.isAttached())
				{
					edit = true;
					note.flag = setFlag(note.flag, NoteFlag::Attached, false);
				}
				edit |= !note.isHidden();
				note.flag = setFlag(note.flag, NoteFlag::Hidden);
				break;
			case EditHoldStepType::Skip:
				if (cannotSetSkip)
					note.flag = setFlag(note.flag, NoteFlag::NonAttached);
				edit = true;
				note.flag = setFlag(note.flag, NoteFlag::Hidden, false);
				note.flag = setFlag(note.flag, NoteFlag::Attached, true);
				break;
			}
			if (hold)
				updatingHolds.insert(hold->ID);
		}

		for (auto&& holdID : updatingHolds)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			hold.updateJoints(score.notes);
			hold.updateLongs(score.notes);
		}

		if (pushSelectionChanged("Change step type") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setFlick(FlickType flick)
	{
		if (selection.emptyNotes())
			return;

		if (flick != FlickType::FlickTypeCount && flick >= maxFlick())
			return;

		bool edit = false;
		for (auto&& [ID, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			bool canFlick = note.canFlick();

			if (canFlick && note.isHold() && !metadata.isExtendedScore)
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);
				canFlick = hold.steps.back() == ID;

				// Prevent critical hold end if the hold start is not critical
				if (canFlick && note.isCrit() && !note.isTrace() &&
				    !hold.separators.front().isCrit() &&
				    cycleMode(note.flick, maxFlick()) == FlickType::None)
				{
					note.flag = setFlag(note.flag, NoteFlag::Critical, false);
				}
			}

			if (canFlick)
			{
				if (flick == FlickType::FlickTypeCount)
				{
					note.flick = cycleMode(note.flick, maxFlick());
					edit = true;
				}
				else
				{
					edit |= note.flick != flick;
					note.flick = flick;
				}
			}
		}

		if (pushSelectionChanged("Change flick") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setEase(EaseType ease)
	{
		if (selection.emptyNotes())
			return;

		if (ease != EaseType::EaseTypeCount && ease >= EaseType::EaseInOut &&
		    !metadata.isExtendedScore)
			return;

		bool edit = false;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (!note.hasEase())
				continue;
			if (ease == EaseType::EaseTypeCount)
			{
				edit = true;
				note.ease = cycleMode(note.ease, maxEase());
			}
			else
			{
				edit |= note.ease != ease;
				note.ease = ease;
			}
		}

		if (pushSelectionChanged("Change ease") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setSoundEffect(SoundEffectType sound)
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;

		bool edit = false;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (!note.canSoundEffect())
				continue;

			edit |= note.soundEffect != sound;
			note.soundEffect = sound;
		}

		if (pushSelectionChanged("Change sound effect") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setFadeType(FadeType fade)
	{
		if (selection.emptyNotes())
			return;

		bool edit = false;
		std::unordered_set<id_t> updatedHold;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (!note.isHold())
				continue;
			if (updatedHold.emplace(note.holdID).second)
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);
				if (hold.fadeType != fade)
				{
					hold.fadeType = fade;
					hold.updateFading(score.notes);
					edit = true;
				}
			}
		}

		if (pushSelectionChanged("Change fade") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setGuideColor(GuideColor color)
	{
		if (selection.emptyNotes())
			return;

		bool edit = false;
		std::unordered_set<HoldNoteStep*> updatedStep;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;

			if (!note.isHold())
				continue;
			HoldNote& hold = score.holdNotes.at(note.holdID);
			HoldNoteStep& step = hold.holdStepAt(note, score.notes);
			if (!updatedStep.emplace(&step).second || !step.isGuide())
				continue;

			if (!metadata.isExtendedScore)
			{
				if (color == GuideColor::GuideColorCount)
				{
					edit = true;
					if (step.guideColor == GuideColor::Yellow)
						step.guideColor = GuideColor::Green;
					else
						step.guideColor = GuideColor::Yellow;
				}
				else if (color == GuideColor::Green || color == GuideColor::Yellow)
				{
					edit |= step.guideColor != color;
					step.guideColor = color;
				}
			}
			else
			{
				if (color == GuideColor::GuideColorCount)
				{
					edit = true;
					step.guideColor = cycleMode(step.guideColor, GuideColor::GuideColorCount);
				}
				else
				{
					edit |= step.guideColor != color;
					step.guideColor = color;
				}
			}
			hold.updateFading(score.notes);
		}

		if (pushSelectionChanged("Change guide") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setLayer(int layer)
	{
		if (selection.empty())
			return;

		// The hi-speeds move to other keys, so the selection changes along with the edit
		SelectionRef selectionBefore = getSelectionSnapshot();
		ScoreCapture beforeCapture = selectionCapture;

		for (auto&& [_, pnote] : selectedNotes)
			pnote->layer = layer;

		HiSpeedRefCollection deletingHiSpeed = std::move(selection.hispeeds);
		clearHiSpeedSelection();
		for (auto it = deletingHiSpeed.begin(), end = deletingHiSpeed.end(); it != end;)
		{
			auto&& [curLayer, tick] = *(it++);
			HiSpeedCollection& curCollection = score.layers[curLayer].hiSpeedChanges;
			HiSpeedCollection& newCollection = score.layers[layer].hiSpeedChanges;
			HiSpeed& hispeed = curCollection.at(tick);

			if (hispeed.layer != layer && newCollection.count(tick) == 0)
			{
				hispeed.layer = layer;
				newCollection.insert(curCollection.extract(tick));
			}
			selectHiSpeed(hispeed);
		}

		if (pushSelectionEdit("Change layer", std::move(beforeCapture), std::move(selectionBefore)))
			updateSelectionFlag();
	}

	void ScoreContext::setCriticals(int critical)
	{
		if (selection.emptyNotes())
			return;
		bool (*setFunc)(bool) = critical < 0 ? flip : critical > 0 ? set : unset;

		bool edit = false;
		std::unordered_map<HoldNoteStep*, HoldNote*> holdSteps;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (note.isHold())
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);
				HoldNoteStep& step = hold.holdStepAt(note, score.notes);
				holdSteps.emplace(&step, &hold);
			}
			else if (!note.canCrit())
				continue;
			bool oldState = note.isCrit();
			bool newState = setFunc(oldState);
			note.flag = setFlag(note.flag, NoteFlag::Critical, newState);
			edit |= newState != oldState;
		}

		for (auto& [pstep, phold] : holdSteps)
		{
			// flip critical state
			if (pstep->isGuide())
			{
				if (!metadata.isExtendedScore)
				{
					if (pstep->guideColor == GuideColor::Yellow)
					{
						pstep->guideColor = GuideColor::Green;
					}
					else
					{
						pstep->guideColor = GuideColor::Yellow;
					}
				}
			}
			else
			{
				id_t startID = pstep->ID, endID;
				auto stepIt = std::find(phold->steps.begin(), phold->steps.end(), pstep->ID);
				size_t nextSepIdx = std::distance(phold->separators.data(), pstep) + 1;
				endID =
				    nextSepIdx < phold->separators.size() ? phold->separators[nextSepIdx].ID : -1;
				auto endStepIt = std::find(stepIt, phold->steps.end(), endID);
				if (!metadata.isExtendedScore)
				{
					// if the hold start is critical, every note in the hold must be critical
					Note& start = score.notes.at(startID);
					Note& end = score.notes.at(phold->steps.back());
					bool endSelected = selection.has(end);
					bool endSpecial = end.isTrace() || end.isFlick();
					bool isCrit = endSelected && endSpecial
					                  ? hasFlag(start.flag, NoteFlag::Critical)
					                  : setFunc(hasFlag(pstep->flag, HoldNoteFlag::Critical));
					bool flipEnd =
					    endSelected && endSpecial && hasFlag(end.flag, NoteFlag::Critical);

					pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Critical, isCrit);
					for (; stepIt != endStepIt; ++stepIt)
					{
						Note& note = score.notes.at(*stepIt);
						note.flag = setFlag(note.flag, NoteFlag::Critical, isCrit);
					}
					if (flipEnd)
						end.flag |= NoteFlag::Critical;
				}
				else if (selection.hasNoteID(startID))
				{
					Note& start = score.notes.at(startID);
					bool isCrit = hasFlag(start.flag, NoteFlag::Critical);
					pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Critical, isCrit);
					for (; stepIt != endStepIt; ++stepIt)
					{
						Note& note = score.notes.at(*stepIt);
						note.flag = setFlag(note.flag, NoteFlag::Critical, isCrit);
					}
				}
			}
		}

		if (pushSelectionChanged("Change critical note") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setCriticalHold(int critical)
	{
		if (selection.emptyNotes())
			return;
		bool (*setFunc)(bool) = critical < 0 ? flip : critical > 0 ? set : unset;

		bool edit = false;
		std::unordered_map<HoldNoteStep*, HoldNote*> holdSteps;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (note.isHold())
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);
				HoldNoteStep& step = hold.holdStepAt(note, score.notes);
				if (!step.isGuide())
					holdSteps.emplace(&step, &hold);
			}
		}

		for (auto& [pstep, phold] : holdSteps)
		{
			bool oldState = pstep->isCrit();
			bool newState = setFunc(oldState);
			pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Critical, newState);
			edit |= newState != oldState;

			if (!metadata.isExtendedScore)
			{
				for (auto step : phold->steps)
				{
					Note& note = score.notes.at(step);
					note.flag = setFlag(note.flag, NoteFlag::Critical, newState);
				}
			}
		}

		if (pushSelectionChanged("Change critical hold") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setFriction(int friction)
	{
		if (selection.emptyNotes())
			return;
		bool (*setFunc)(bool) = friction < 0 ? flip : friction > 0 ? set : unset;

		bool edit = false;
		for (auto&& [_, pnote] : selectedNotes)
		{
			// Hold steps and invisible hold points cannot be trace notes
			Note& note = *pnote;
			if (note.type != NoteType::Tap)
				continue;

			if (note.isHold())
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);

				if (note.type == NoteType::Tap && note.isHidden() &&
				    (metadata.isExtendedScore || !hold.separators.front().isGuide()))
					note.flag = setFlag(note.flag, NoteFlag::Hidden, false);

				// Prevent critical hold end if the hold start is not critical
				if (!metadata.isExtendedScore && note.isCrit() && note.isTrace() &&
				    !note.isFlick() && note.ID == hold.steps.back() &&
				    !hold.separators.front().isCrit())
					note.flag = setFlag(note.flag, NoteFlag::Critical, false);
			}

			if (!note.canTrace())
				continue;
			bool oldState = hasFlag(note.flag, NoteFlag::Trace);
			bool newState = setFunc(oldState);
			note.flag = setFlag(note.flag, NoteFlag::Trace, newState);
			edit |= oldState != newState;
		}

		if (pushSelectionChanged("Change trace notes") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setDummy(int dummy)
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;
		bool (*setFunc)(bool) = dummy < 0 ? flip : dummy > 0 ? set : unset;

		bool edit = false;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (!note.canDummy())
				continue;

			bool oldState = note.isDummy();
			bool newState = setFunc(oldState);
			note.flag = setFlag(note.flag, NoteFlag::Dummy, newState);
			edit |= oldState != newState;
		}

		if (pushSelectionChanged("Change dummy note") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setDummyHold(int dummy)
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;
		bool (*setFunc)(bool) = dummy < 0 ? flip : dummy > 0 ? set : unset;

		bool edit = false;
		std::unordered_map<HoldNoteStep*, HoldNote*> holdSteps;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (note.isHold())
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);
				HoldNoteStep& step = hold.holdStepAt(note, score.notes);
				if (!step.isGuide())
					holdSteps.emplace(&step, &hold);
			}
		}

		for (auto& [pstep, phold] : holdSteps)
		{
			bool oldState = pstep->isDummy();
			bool newState = setFunc(oldState);
			pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Dummy, newState);
			edit |= oldState != newState;
		}

		if (pushSelectionChanged("Change dummy hold") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setGuideAlpha(float alpha)
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;

		bool edit = false;
		std::unordered_set<id_t> holds;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (!note.isHold())
				continue;
			const HoldNote& hold = score.holdNotes.at(note.holdID);
			if (hold.fadeType != FadeType::Custom || !hold.canSetGuideAlpha(note, score.notes))
				continue;
			edit |= pnote->guideAlpha != alpha;
			pnote->guideAlpha = alpha;
			holds.emplace(note.holdID);
		}

		for (id_t holdID : holds)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			hold.updateFading(score.notes);
			hold.updateLongs(score.notes);
		}

		if (pushSelectionChanged("Change guide alpha") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setHoldLayer(HoldStepLayer layer)
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;

		bool edit = false;
		std::unordered_set<HoldNoteStep*> updatedStep;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;

			if (!note.isHold())
				continue;
			HoldNote& hold = score.holdNotes.at(note.holdID);
			HoldNoteStep& step = hold.holdStepAt(note, score.notes);
			if (!updatedStep.emplace(&step).second)
				continue;

			edit |= step.layer == layer;
			step.layer = layer;
		}

		if (pushSelectionChanged("Change hold step layer") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::setHoldSeparator(int separator)
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;

		bool (*setFunc)(bool) = separator < 0 ? flip : separator > 0 ? set : unset;
		bool edit = false;
		std::unordered_set<id_t> holds;
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			if (!note.isHold())
				continue;

			HoldNote& hold = score.holdNotes.at(note.holdID);
			auto it = std::upper_bound(hold.separators.begin(), hold.separators.end(), note,
			                           HoldNote::HoldStepComparer(score.notes));
			const HoldNoteStep& holdStep = *std::prev(it);
			bool isSeparator = holdStep.ID == note.ID;
			bool wantEdit = isSeparator != setFunc(isSeparator);
			// First and last note cannot be set/unset as separator
			bool cannotEdit =
			    (std::prev(it) == hold.separators.begin() && setFunc(isSeparator) == false) ||
			    (note.ID == hold.steps.back() && setFunc(isSeparator) == true);
			if (!wantEdit || cannotEdit)
				continue;

			edit |= wantEdit;
			if (!isSeparator)
				hold.separators.insert(it, holdStep)->ID = note.ID;
			else
			{
				note.flag = setFlag(note.flag, NoteFlag::LongNote, false);
				hold.separators.erase(std::prev(it));
			}
			holds.emplace(note.holdID);
		}

		for (id_t holdID : holds)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			hold.updateSeparators(score.notes);
			hold.updateFading(score.notes);
			hold.updateLongs(score.notes);
		}

		if (pushSelectionChanged("Set separator") || edit)
			updateSelectionFlag();
	}

	void ScoreContext::updateSelectionFlag()
	{
		selectedFlag = setFlag(selectedFlag, SelectionFlag::DirtyProperty);
		auto begin = selectedNotes.begin(), end = selectedNotes.end();
		auto canFlick = [this](const NoteViewCollection::value_type& nv)
		{
			return nv.second->canFlick() &&
			       (metadata.isExtendedScore || !nv.second->isHold() ||
			        score.holdNotes.at(nv.second->holdID).steps.front() != nv.first);
		};
		auto hasEase = [this](const NoteViewCollection::value_type& nv)
		{
			if (!nv.second->hasEase())
				return false;
			const auto& joints = score.holdNotes.at(nv.second->holdID).joints;
			return std::find(joints.begin(), joints.end(), nv.first) != joints.end();
		};
		auto isHoldMid = [this](const NoteViewCollection::value_type& nv)
		{
			if (!nv.second->isHold())
				return false;
			const auto& steps = score.holdNotes.at(nv.second->holdID).steps;
			return nv.first != steps.front() && nv.first != steps.back();
		};
		auto isHoldNoteStep = [this](const NoteViewCollection::value_type& nv)
		{
			if (!nv.second->isHold())
				return false;
			const auto& hold = score.holdNotes.at(nv.second->holdID);
			return nv.first == hold.holdStepAt(*nv.second, score.notes).ID;
		};
		auto isNormalHold = [this](const NoteViewCollection::value_type& nv)
		{
			return nv.second->isHold() && !score.holdNotes.at(nv.second->holdID)
			                                   .holdStepAt(*nv.second, score.notes)
			                                   .isGuide();
		};
		auto isGuideHold = [this](const NoteViewCollection::value_type& nv)
		{
			return nv.second->isHold() && score.holdNotes.at(nv.second->holdID)
			                                  .holdStepAt(*nv.second, score.notes)
			                                  .isGuide();
		};
		auto hasGuideAlpha = [this](const NoteViewCollection::value_type& nv)
		{
			return nv.second->isHold() &&
			       score.holdNotes.at(nv.second->holdID).canGuideAlpha(*nv.second, score.notes);
		};
		auto canSetGuideAlpha = [this](const NoteViewCollection::value_type& nv)
		{
			return nv.second->isHold() &&
			       score.holdNotes.at(nv.second->holdID).canSetGuideAlpha(*nv.second, score.notes);
		};
		auto canConnectHold = [this]()
		{
			if (selectedNotes.size() != 2)
				return false;
			const Note& n1 = *selectedNotes.begin()->second;
			const Note& n2 = *(++selectedNotes.begin())->second;
			if (!n1.isHold() || !n2.isHold())
				return false;
			const HoldNote& h1 = score.holdNotes.at(n1.holdID);
			const HoldNote& h2 = score.holdNotes.at(n2.holdID);
			if (n1.tick == n2.tick)
				return n1.ID == h1.steps.front() && n2.ID == h2.steps.back() ||
				       n2.ID == h2.steps.front() && n1.ID == h1.steps.back();
			else if (n1.tick < n2.tick)
				return n2.ID == h2.steps.front() && n1.ID == h1.steps.back();
			else
				return n1.ID == h1.steps.front() && n2.ID == h2.steps.back();
		};
		selectedFlag =
		    setFlag(selectedFlag, SelectionFlag::CanTrace,
		            std::any_of(begin, end, [](auto&& nv) { return nv.second->canTrace(); }));
		selectedFlag =
		    setFlag(selectedFlag, SelectionFlag::CanCritical,
		            std::any_of(begin, end, [](auto&& nv) { return nv.second->canCrit(); }));
		selectedFlag =
		    setFlag(selectedFlag, SelectionFlag::CanFlick, std::any_of(begin, end, canFlick));
		selectedFlag =
		    setFlag(selectedFlag, SelectionFlag::CanDummy,
		            metadata.isExtendedScore &&
		                std::any_of(begin, end, [](auto&& nv) { return nv.second->canDummy(); }));
		selectedFlag = setFlag(
		    selectedFlag, SelectionFlag::CanSoundEffect,
		    metadata.isExtendedScore &&
		        std::any_of(begin, end, [](auto&& nv) { return nv.second->canSoundEffect(); }));
		selectedFlag =
		    setFlag(selectedFlag, SelectionFlag::CanEase, std::any_of(begin, end, hasEase));
		selectedFlag =
		    setFlag(selectedFlag, SelectionFlag::HasAnyHoldMid, std::any_of(begin, end, isHoldMid));
		selectedFlag = setFlag(selectedFlag, SelectionFlag::HasAnyHoldNoteStep,
		                       std::any_of(begin, end, isHoldNoteStep));
		selectedFlag = setFlag(selectedFlag, SelectionFlag::HasHoldNote,
		                       std::any_of(begin, end, isNormalHold));
		selectedFlag = setFlag(selectedFlag, SelectionFlag::HasGuideNote,
		                       std::any_of(begin, end, isGuideHold));
		selectedFlag = setFlag(selectedFlag, SelectionFlag::HasGuideAlphaNote,
		                       std::any_of(begin, end, hasGuideAlpha));
		selectedFlag = setFlag(selectedFlag, SelectionFlag::CanSetGuideAlphaNote,
		                       std::any_of(begin, end, canSetGuideAlpha));
		selectedFlag = setFlag(selectedFlag, SelectionFlag::CanConnectHold, canConnectHold());
	}

	tick_t ScoreContext::getMinTickFromSelection() const
	{
		using note_view_t = NoteViewCollection::value_type;
		using hispeed_ref_t = HiSpeedRefCollection::value_type;
		auto minNoteIt = std::min_element(selectedNotes.begin(), selectedNotes.end(),
		                                  [](const note_view_t& nv1, const note_view_t& nv2)
		                                  { return nv1.second->tick < nv2.second->tick; });
		tick_t minTick = minNoteIt == selectedNotes.end() ? MAX_TICK : minNoteIt->second->tick;

		auto hispeedComp = [&](const hispeed_ref_t& hs1, const hispeed_ref_t& hs2)
		{
			return score.layers[hs1.first].hiSpeedChanges.at(hs1.second).tick <
			       score.layers[hs2.first].hiSpeedChanges.at(hs2.second).tick;
		};
		auto minHspdIt =
		    std::min_element(selection.hispeeds.begin(), selection.hispeeds.end(), hispeedComp);
		if (minHspdIt == selection.hispeeds.end())
			return minTick;
		return std::min(score.layers[minHspdIt->first].hiSpeedChanges.at(minHspdIt->second).tick,
		                minTick);
	}

	void ScoreContext::clearNoteSelection()
	{
		selection.clearNotes();
		selectedNotes.clear();
		selectionCapture.clearNotes();
	}

	void ScoreContext::clearHiSpeedSelection()
	{
		selection.clearHispeed();
		selectionCapture.clearHispeeds();
	}

	void ScoreContext::clearSelection()
	{
		selection.clearAll();
		selectedNotes.clear();
		selectionCapture.clear();
	}

	void ScoreContext::updateSelectionCapture()
	{
		selectionCapture.clear();
		for (const auto& [ID, pnote] : selectedNotes)
		{
			selectionCapture.captureNote(score, pnote->ID);
			if (pnote->isHold())
				selectionCapture.captureHold(score, pnote->holdID);
		}
		for (const auto& key : selection.hispeeds)
			selectionCapture.captureHiSpeed(score, key);
	}

	void ScoreContext::assertNoteSelection()
	{
		// Assert that selection view still match the selection
		assert(selection.notes.size() == selectedNotes.size());
		// Quitely fix the invariant in the release build
		if (selection.notes.size() != selectedNotes.size())
			updateSelectionView();
	}

	void ScoreContext::selectNote(Note& note, bool update)
	{
		auto&& [_, emplaced] = selection.notes.insert(note.ID);
		if (!emplaced)
			return;
		// Update our views
		selectedNotes.emplace(note.ID, &note);
		selectionCapture.captureNote(score, note.ID);
		if (note.isHold())
			selectionCapture.captureHold(score, note.holdID);
		if (update)
			updateSelectionFlag();
	}

	void ScoreContext::selectHiSpeed(const HiSpeed& hispeed)
	{
		layered_tick_t key{ hispeed.layer, hispeed.tick };
		if (selection.hispeeds.insert(key).second)
			selectionCapture.captureHiSpeed(score, key);
	}

	void ScoreContext::deselectNote(const Note& note, bool update)
	{
		selection.notes.erase(note.ID);
		selectedNotes.erase(note.ID);
		if (update)
			updateSelectionFlag();
	}

	void ScoreContext::deselectHiSpeed(const HiSpeed& hispeed)
	{
		selection.hispeeds.erase({ hispeed.layer, hispeed.tick });
	}

	void ScoreContext::selectAll(id_t layer)
	{
		clearSelection();

		for (auto&& [ID, note] : score.notes)
			if (layer == LAYER_ALL || note.layer == layer)
				selectNote(note, false);

		for (id_t l = 0; l < score.layers.size(); ++l)
			if (layer == LAYER_ALL || l == layer)
				for (auto&& [tick, hispeed] : score.layers[l].hiSpeedChanges)
					selectHiSpeed(hispeed);

		updateSelectionFlag();
	}

	void ScoreContext::deselectAll()
	{
		clearSelection();
		updateSelectionFlag();
	}

	void ScoreContext::updateSelectionView()
	{
		selectedNotes.clear();
		selectedNotes.reserve(selection.notes.size());
		for (auto it = selection.notes.begin(); it != selection.notes.end();)
		{
			auto noteIt = score.notes.find(*it);
			if (noteIt == score.notes.end())
			{
				// Selected note no longer exists
				it = selection.notes.erase(it);
				continue;
			}
			selectedNotes.emplace(noteIt->first, &noteIt->second);
			++it;
		}
		// Same for hi-speeds, e.g. after undoing the insertion of a selected one
		for (auto it = selection.hispeeds.begin(); it != selection.hispeeds.end();)
		{
			const auto& [layer, tick] = *it;
			if (layer < 0 || layer >= static_cast<id_t>(score.layers.size()) ||
			    score.layers[layer].hiSpeedChanges.count(tick) == 0)
				it = selection.hispeeds.erase(it);
			else
				++it;
		}
		updateSelectionCapture();
	}

	void ScoreContext::deleteSelection()
	{
		if (selection.empty())
			return;

		SelectionRef selectionBefore = getSelectionSnapshot();
		ScoreCapture beforeCapture = std::move(selectionCapture), afterCapture;
		std::unordered_set<id_t> updatingHolds;
		NoteRefCollection deletingNotes = std::move(selection.notes);
		HiSpeedRefCollection deletingHiSpeed = std::move(selection.hispeeds);
		clearSelection();
		hoveringNotes.clear();

		for (auto&& ID : deletingNotes)
		{
			auto noteIt = score.notes.find(ID);
			if (noteIt == score.notes.end())
				// Note deleted or doesn't exist
				continue;
			Note& note = noteIt->second;
			if (note.isHold())
			{
				HoldNote& hold = score.holdNotes.at(note.holdID);
				if (hold.steps.size() == 2)
				{
					if (hold.steps.front() == ID)
						eraseNote(score.notes.at(hold.steps.back()), false);
					else
						eraseNote(score.notes.at(hold.steps.front()), false);
					score.holdNotes.erase(hold.ID);
				}
				else
				{
					// Erase the hold step
					auto it = std::find(hold.steps.begin(), hold.steps.end(), note.ID);
					assert(it != hold.steps.end() && "Note is not part of the hold?");
					if (!metadata.isExtendedScore)
					{
						// Make sure you only delete tick note in normal score
						if (*it == hold.steps.front())
							swapNoteProperties(note, score.notes.at(*std::next(it)));
						else if (*it == hold.steps.back())
							swapNoteProperties(note, score.notes.at(*std::prev(it)));
					}
					hold.steps.erase(it);
					// Erase the separator step
					auto stepIt = std::find_if(
					    std::next(hold.separators.begin()), hold.separators.end(),
					    [ID = note.ID](const HoldNoteStep& step) { return step.ID == ID; });
					if (stepIt != hold.separators.end())
						hold.separators.erase(stepIt);

					updatingHolds.emplace(hold.ID);
				}
			}
			eraseNote(note, false);
		}
		for (auto&& ID : updatingHolds)
		{
			auto holdIt = score.holdNotes.find(ID);
			if (holdIt == score.holdNotes.end())
				// Hold deleted after or doesn't exist
				continue;
			HoldNote& hold = holdIt->second;
			hold.sortSteps(score.notes, !metadata.isExtendedScore);
			afterCapture.captureHold(score, ID);
		}
		for (auto& [layer, tick] : deletingHiSpeed)
		{
			score.layers[layer].hiSpeedChanges.erase(tick);
		}

		updateSelectionFlag();
		if (auto edit = beforeCapture.diffAll(afterCapture))
			pushEdit("Delete notes", toHistoryEdit(std::move(*edit)), std::move(selectionBefore));
	}

	void ScoreContext::flipSelection()
	{
		if (selection.emptyNotes())
			return;

		static_assert(int(FlickType::FlickTypeCount) == 7, "Make sure nothing broke here!");
		for (auto&& [_, pnote] : selectedNotes)
		{
			Note& note = *pnote;
			note.lane = ScoreEditorTimeline::NUM_LANES - note.lane - note.width;

			switch (note.flick)
			{
			case FlickType::Left:
				note.flick = FlickType::Right;
				break;
			case FlickType::Right:
				note.flick = FlickType::Left;
				break;
			case FlickType::DownLeft:
				note.flick = FlickType::DownRight;
				break;
			case FlickType::DownRight:
				note.flick = FlickType::DownLeft;
				break;
			}
		}

		pushHistory("Flip notes");
	}

	void ScoreContext::cutSelection()
	{
		copySelection();
		deleteSelection();
	}

	void ScoreContext::copySelection() const
	{
		if (selection.empty())
			return;

		json data;
		try
		{
			selected_score_to_json(data, score, selection, getMinTickFromSelection(),
			                       selectedLayer);
		}
		catch (const std::exception& ex)
		{
			IO::messageBox(APP_NAME, ex.what(), IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Error);
			return;
		}
		std::string clipboard{ clipboardSignature };
#ifdef DEBUG
		int indent = 2;
#else
		int indent = -1;
#endif
		clipboard.append("\n").append(data.dump(indent));

		ImGui::SetClipboardText(clipboard.c_str());
	}

	void ScoreContext::paste(PasteData& pasteData, float offsetLane, tick_t offsetTick, id_t holdID)
	{
		SelectionRef selectionBefore = getSelectionSnapshot();
		clearSelection();
		ScoreCapture beforeCapture;

		bool simplePaste = pasteData.holdNotes.size() == 1 ||
		                   pasteData.holdNotes.empty() && pasteData.notes.size();

		if (holdID >= 0 && simplePaste)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			beforeCapture.captureHold(score, holdID);
			for (const auto& [_, note] : pasteData.notes)
			{
				Note insNote = note;
				insNote.lane += offsetLane;
				insNote.tick += offsetTick;
				if (!metadata.isExtendedScore)
					insNote.type = NoteType::Tick; // Force compatibility
				Note* newNote = insertNote(insNote, holdID, false);
				if (newNote)
					selectNote(*newNote, false);
			}
			hold.updateSeparators(score.notes);
			hold.updateJoints(score.notes);
			hold.updateLongs(score.notes);
			hold.updateFading(score.notes);
		}
		else
		{
			// update IDs and copy notes
			for (const auto& [_, note] : pasteData.notes)
			{
				if (note.isHold())
					continue;
				Note insNote = note;
				insNote.lane += offsetLane;
				insNote.tick += offsetTick;
				Note* newNote = insertNote(insNote, -1, false);
				if (newNote)
					selectNote(*newNote, false);
			}

			std::unordered_map<id_t, id_t> remappedID;
			remappedID.reserve(std::min(pasteData.notes.size() - score.notes.size(), 0x8000ull));
			std::vector<id_t> holdChainStart;
			for (const auto& [_, hold] : pasteData.holdNotes)
			{
				auto stepIt = hold.steps.begin(), endIt = hold.steps.end();
				Note start = pasteData.notes.at(*(stepIt++));
				start.lane += offsetLane;
				start.tick += offsetTick;
				Note end = pasteData.notes.at(*(--endIt));
				end.lane += offsetLane;
				end.tick += offsetTick;
				auto&& [newHold, newStart, newEnd] = insertHold(start, end, hold, false);
				selectNote(newStart, false);
				selectNote(newEnd, false);
				for (; stepIt != endIt; ++stepIt)
				{
					Note step = pasteData.notes.at(*stepIt);
					step.lane += offsetLane;
					step.tick += offsetTick;
					if (!metadata.isExtendedScore)
						step.type = NoteType::Tick; // Force compatibility
					Note* newStep = insertNote(step, newHold.ID, false);
					if (newStep)
					{
						selectNote(*newStep, false);
						remappedID[step.ID] = newStep->ID;
					}
				}
				if (metadata.isExtendedScore)
				{
					for (auto it = std::next(hold.separators.begin()), end = hold.separators.end();
					     it != end; ++it)
					{
						const auto& separator = *it;
						auto newIt = remappedID.find(separator.ID);
						if (newIt == remappedID.end())
							continue;
						auto& newSeparator = newHold.separators.emplace_back(separator);
						newSeparator.ID = newIt->second;
					}
				}
				newHold.updateJoints(score.notes);
				newHold.updateLongs(score.notes);
				newHold.updateFading(score.notes);
			}

			for (const auto& [_, hispeed] : pasteData.hiSpeedChanges)
			{
				HiSpeed newHispeed = hispeed;
				newHispeed.tick += offsetTick;
				beforeCapture.captureHiSpeed(score, { newHispeed.layer, newHispeed.tick });
				const HiSpeed& inserted = insertHispeedChange(newHispeed, false);
				// select newly pasted
				selectHiSpeed(inserted);
			}
		}

		updateSelectionFlag();
		pasteData.cancelPaste();
		pushSelectionEdit("Paste notes", std::move(beforeCapture), std::move(selectionBefore));
	}

	bool ScoreContext::canMoveNoteSelection(tick_t& ticks, int quarterDivision, float& lanes,
	                                        float laneDivision, SnapMode snapMode)
	{
		const float laneMin = minLane();
		switch (snapMode)
		{
		default:
		case SnapMode::Relative:
			if (lanes == 0 && ticks == 0)
				return false;
			for (auto&& [_, pnote] : selectedNotes)
			{
				const Note& n = *pnote;
				float lane = n.lane + lanes;
				tick_t tick = n.tick + ticks;
				if (!isWithinRange(lane, laneMin, maxLane(n.width)))
				{
					lanes = std::clamp(lanes, laneMin - n.lane, maxLane(n.width) - n.lane);
					if (lanes == 0 && ticks == 0)
						return false;
				}
				if (tick < 0)
				{
					ticks = std::max(ticks, -n.tick);
					if (lanes == 0 && ticks == 0)
						return false;
				}
			}
			return true;
		case SnapMode::Absolute:
			if (lanes == 0 && ticks == 0)
				return false;
			for (auto&& [_, pnote] : selectedNotes)
			{
				const Note& n = *pnote;
				float lane = n.lane + lanes;
				tick_t tick = n.tick + ticks;
				if (!isWithinRange(lane, laneMin, maxLane(n.width)))
				{
					lanes = std::clamp(lanes, laneMin - n.lane, maxLane(n.width) - n.lane);
					if (lanes == 0 && ticks == 0)
						return false;
				}
				if (tick < 0)
				{
					ticks = std::max(ticks, -n.tick);
					if (lanes == 0 && ticks == 0)
						return false;
				}
			}
			return true;
		case SnapMode::IndividualAbsolute:
		{
			if (lanes == 0 && ticks == 0)
				return false;
			constexpr auto noop = [](float x) { return x; };
			float (*laneSnapFn)(float) = lanes > 0 ? ceilf : floorf;
			float (*quatSnapFn)(float) = ticks > 0 ? ceilf : floorf;
			if (ticks == 0 && lanes == 0)
				return false;
			else if (lanes == 0)
				laneSnapFn = noop;
			else if (ticks == 0)
				quatSnapFn = noop;
			float offsetLane = laneSnapFn(lanes * laneDivision) / laneDivision;
			tick_t offsetTick = quartersToTicks(
			    quatSnapFn(ticksToQuarters(ticks) * quarterDivision) / quarterDivision);
			for (auto&& [_, pnote] : selectedNotes)
			{
				const Note& n = *pnote;
				float lane = laneSnapFn(n.lane * laneDivision) / laneDivision;
				tick_t tick = quartersToTicks(
				    quatSnapFn(ticksToQuarters(n.tick) * quarterDivision) / quarterDivision);
				if (n.lane == lane && lanes != 0)
					lane += offsetLane;
				if (n.tick == tick && ticks != 0)
					tick += offsetTick;
				if (!isWithinRange(lane, laneMin, maxLane(n.width)) || tick < 0)
					return false;
			}
			return true;
		}
		}
	}

	void ScoreContext::moveNoteSelection(tick_t ticks, int quarterDivision, float lanes,
	                                     float laneDivision, SnapMode snapMode, bool update)
	{
		std::vector<NoteOrderedCollection::node_type> updatingNodes;
		updatingNodes.reserve(selectedNotes.size());
		for (auto&& [ID, pnote] : selectedNotes)
		{
			auto&& [it, end] = notesOrderedView.equal_range(pnote->tick);
			it = std::find_if(it, end, [=](const NoteOrderedCollection::value_type& v)
			                  { return pnote == v.second; });
			updatingNodes.emplace_back(notesOrderedView.extract(it));
		}

		switch (snapMode)
		{
		default:
			for (auto&& [_, pnote] : selectedNotes)
			{
				pnote->lane += lanes;
				pnote->tick += ticks;
			}
			break;
		case SnapMode::Absolute:
			for (auto&& [_, pnote] : selectedNotes)
			{
				pnote->lane += lanes;
				pnote->tick += ticks;
			}
			break;
		case SnapMode::IndividualAbsolute:
		{
			constexpr auto noop = [](float x) { return x; };
			auto laneSnapFn = lanes == 0 ? noop : lanes > 0 ? ceilf : floorf;
			auto quatSnapFn = ticks == 0 ? noop : ticks > 0 ? ceilf : floorf;
			float offsetLane = laneSnapFn(lanes * laneDivision) / laneDivision;
			tick_t offsetTick = quartersToTicks(
			    quatSnapFn(ticksToQuarters(ticks) * quarterDivision) / quarterDivision);
			for (auto&& [_, pnote] : selectedNotes)
			{
				Note& n = *pnote;
				float lane = laneSnapFn(n.lane * laneDivision) / laneDivision;
				tick_t tick = quartersToTicks(
				    quatSnapFn(ticksToQuarters(n.tick) * quarterDivision) / quarterDivision);
				if (n.lane == lane && lanes != 0)
					n.lane += offsetLane;
				else
					n.lane = lane;
				if (n.tick == tick && ticks != 0)
					n.tick += offsetTick;
				else
					n.tick = tick;
			}
			break;
		}
		}

		std::unordered_set<id_t> updatedHolds;
		for (auto&& node : updatingNodes)
		{
			Note& n = *node.mapped();
			node.key() = n.tick;
			notesOrderedView.insert(std::move(node));

			if (!n.isHold() && updatedHolds.count(n.holdID) == 0)
				continue;
			HoldNote& hold = score.holdNotes.at(n.holdID);
			hold.sortSteps(score.notes, !metadata.isExtendedScore);
			updatedHolds.emplace(hold.ID);
		}

		if (update)
			pushSelectionChanged("Move note");
	}

	void ScoreContext::setPosNoteSelection(tick_t tick, float lane, bool update)
	{
		bool setTick = tick < MAX_TICK, setLane = isfinite(lane);
		if (!setTick && !setLane)
			return;
		std::vector<NoteOrderedCollection::node_type> updatingNodes;
		if (setTick)
		{
			updatingNodes.reserve(selectedNotes.size());
			for (auto&& [ID, pnote] : selectedNotes)
			{
				auto&& [it, end] = notesOrderedView.equal_range(pnote->tick);
				it = std::find_if(it, end, [=](const NoteOrderedCollection::value_type& v)
				                  { return pnote == v.second; });
				updatingNodes.emplace_back(notesOrderedView.extract(it));
			}
			tick = std::clamp(tick, 0, MAX_TICK);
		}

		std::unordered_set<id_t> updatingHolds;
		for (auto&& [ID, pnote] : selectedNotes)
		{
			if (setTick)
				pnote->tick = tick;
			if (setLane)
				pnote->lane = std::clamp(lane, minLane(), maxLane(pnote->width));
			if (pnote->isHold())
				updatingHolds.emplace(pnote->holdID);
		}

		for (auto&& node : updatingNodes)
		{
			Note& n = *node.mapped();
			node.key() = n.tick;
			notesOrderedView.insert(std::move(node));
		}

		for (auto&& holdID : updatingHolds)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			hold.sortSteps(score.notes, !metadata.isExtendedScore);
		}

		if (update)
			pushSelectionChanged("Set note postion");
	}

	void PasteData::cancelPaste()
	{
		pasting = false;
		notesOrderedView.clear();
		notes.clear();
		holdNotes.clear();
		hiSpeedChanges.clear();
	}

	void PasteData::startPaste()
	{
		const char* clipboardDataPtr = ImGui::GetClipboardText();
		if (clipboardDataPtr == nullptr)
			return;

		std::stringstream clipboardDataStream;
		clipboardDataStream << clipboardDataPtr;
		clipboardDataStream.seekg(0, std::ios::beg);
		std::string signature;

		if (!std::getline(clipboardDataStream, signature) ||
		    !IO::startsWith(signature, clipboardSignature))
			return;

		json data = json::parse(clipboardDataStream, nullptr, false);
		if (data.is_discarded())
			return;

		load(data);
	}

	void PasteData::load(const nlohmann::json& data)
	{
		try
		{
			paste_data_from_json(data, *this);
			updatePasteSize();
		}
		catch (const std::exception& ex)
		{
			cancelPaste();
			IO::messageBox(APP_NAME, ex.what(), IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Error);
		}
		notesOrderedView.clear();
		for (auto& [_, note] : notes)
			notesOrderedView.emplace(note.tick, &note);
	}

	void PasteData::updatePasteSize()
	{
		if (notes.size())
		{
			// treat the paste data object as a big note with lane and width
			auto laneCmp =
			    [](const NoteCollection::value_type& n1, const NoteCollection::value_type& n2)
			{ return n1.second.lane < n2.second.lane; };
			auto minLaneIt = std::min_element(notes.begin(), notes.end(), laneCmp);
			minLane = minLaneIt == notes.end() ? 0 : minLaneIt->second.lane;
			width = 0;
			for (const auto& [_, note] : notes)
				width = std::max(note.lane - minLane + note.width, width);
		}
	}

	void PasteData::flip()
	{
		static_assert(int(FlickType::FlickTypeCount) == 7, "Also make sure nothing broke here!");
		for (auto& [_, note] : notes)
		{
			note.lane = ScoreEditorTimeline::NUM_LANES - note.lane - note.width + 1;

			switch (note.flick)
			{
			case FlickType::Left:
				note.flick = FlickType::Right;
				break;
			case FlickType::Right:
				note.flick = FlickType::Left;
				break;
			case FlickType::DownLeft:
				note.flick = FlickType::DownRight;
				break;
			case FlickType::DownRight:
				note.flick = FlickType::DownLeft;
				break;
			}
		}
		updatePasteSize();
	}

	void ScoreContext::shrinkSelection(tick_t spacing)
	{
		if ((selection.notes.size() + selection.hispeeds.size()) < 2)
			return;
		assertNoteSelection();

		std::unordered_set<id_t> updatingHold;
		std::vector<NoteOrderedCollection::node_type> updatingNodes;
		HiSpeedRefCollection updatingHispeed = std::move(selection.hispeeds);
		selection.hispeeds.clear();
		updatingNodes.reserve(selectedNotes.size());
		for (auto&& [ID, pnote] : selectedNotes)
		{
			auto&& [it, end] = notesOrderedView.equal_range(pnote->tick);
			it = std::find_if(it, end, [=](const NoteOrderedCollection::value_type& v)
			                  { return pnote == v.second; });
			if (it->second->isHold())
				updatingHold.emplace(it->second->holdID);
			updatingNodes.emplace_back(notesOrderedView.extract(it));
		}
		std::stable_sort(updatingNodes.begin(), updatingNodes.end(),
		                 [](const auto& n1, const auto& n2) { return n1.key() < n2.key(); });

		auto shrink = [&](auto ordFunc, auto noteIt, auto endNoteIt, auto hspdIt, auto endHspdIt)
		{
			tick_t baseTick;
			if (noteIt != endNoteIt && hspdIt != endHspdIt)
				baseTick = std::min(noteIt->key(), hspdIt->second, ordFunc);
			else if (noteIt != endNoteIt)
				baseTick = noteIt->key();
			else
				baseTick = hspdIt->second;
			for (tick_t tick = baseTick; noteIt != endNoteIt || hspdIt != endHspdIt;
			     tick += spacing)
			{
				if (noteIt != endNoteIt)
				{
					noteIt->key() = tick;
					noteIt->mapped()->tick = tick;
					notesOrderedView.insert(std::move(*noteIt));
					++noteIt;
				}
				if (hspdIt != endHspdIt &&
				    (tick == hspdIt->second ||
				     score.layers[hspdIt->first].hiSpeedChanges.count(tick) == 0))
				{
					auto& hiSpeedChanges = score.layers[hspdIt->first].hiSpeedChanges;
					auto node = hiSpeedChanges.extract(hspdIt->second);
					node.key() = tick;
					node.mapped().tick = tick;
					hiSpeedChanges.insert(std::move(node));
					selection.hispeeds.emplace(hspdIt->first, tick);
					++hspdIt;
				}
			}
		};

		if (spacing >= 0)
			shrink(std::less(), updatingNodes.begin(), updatingNodes.end(), updatingHispeed.begin(),
			       updatingHispeed.end());
		else
			shrink(std::greater(), updatingNodes.rbegin(), updatingNodes.rend(),
			       updatingHispeed.rbegin(), updatingHispeed.rend());

		for (const auto& holdID : updatingHold)
			score.holdNotes.at(holdID).sortSteps(score.notes, !metadata.isExtendedScore);

		pushHistory("Shrink notes");
	}

	void ScoreContext::compressSelection()
	{
		if (selectedNotes.size() < 2)
			return;
		assertNoteSelection();

		std::unordered_map<id_t, Layer> updatingLayers;
		std::unordered_set<id_t> updatingHold;
		std::vector<NoteOrderedCollection::node_type> updatingNodes;
		HiSpeedRefCollection updatingHispeed = std::move(selection.hispeeds);
		selection.hispeeds.clear();
		updatingNodes.reserve(selectedNotes.size());
		for (auto&& [ID, pnote] : selectedNotes)
		{
			auto&& [it, end] = notesOrderedView.equal_range(pnote->tick);
			it = std::find_if(it, end, [=](const NoteOrderedCollection::value_type& v)
			                  { return pnote == v.second; });
			if (it->second->isHold())
				updatingHold.emplace(it->second->holdID);
			updatingLayers.emplace(it->second->layer, score.layers[it->second->layer]);
			updatingNodes.emplace_back(notesOrderedView.extract(it));
		}
		std::stable_sort(updatingNodes.begin(), updatingNodes.end(),
		                 [](const auto& n1, const auto& n2) { return n1.key() < n2.key(); });

		auto noteIt = updatingNodes.begin(), endNoteIt = updatingNodes.end();
		auto hspdIt = updatingHispeed.begin(), endHspdIt = updatingHispeed.end();
		tick_t baseTick;
		if (noteIt != endNoteIt && hspdIt != endHspdIt)
			baseTick = std::min(noteIt->key(), hspdIt->second);
		else if (noteIt != endNoteIt)
			baseTick = noteIt->key();
		else
			baseTick = hspdIt->second;

		tick_t prevTick = baseTick;
		tick_t tick = baseTick;
		while (noteIt != endNoteIt || hspdIt != endHspdIt)
		{
			tick_t shrinkTick;
			int compare;
			if (noteIt != endNoteIt && hspdIt != endHspdIt)
			{
				compare = noteIt->key() == hspdIt->second  ? 0
				          : noteIt->key() < hspdIt->second ? -1
				                                           : 1;
				shrinkTick = std::min(noteIt->key(), hspdIt->second);
			}
			else
			{
				compare = noteIt != endNoteIt ? -1 : 1;
				shrinkTick = noteIt != endNoteIt ? noteIt->key() : hspdIt->second;
			}
			if (shrinkTick != prevTick)
				++tick;
			// Shrink notes
			if (compare <= 0)
			{
				noteIt->key() = tick;
				noteIt->mapped()->tick = tick;
				notesOrderedView.insert(std::move(*noteIt));
				++noteIt;
			}
			if (compare >= 0)
			{
				auto& hiSpeedChanges = score.layers[hspdIt->first].hiSpeedChanges;
				auto node = hiSpeedChanges.extract(hspdIt->second);
				node.key() = tick;
				node.mapped().tick = tick;
				hiSpeedChanges.erase(node.key());
				hiSpeedChanges.insert(std::move(node));
				selection.hispeeds.emplace(hspdIt->first, tick);
				++hspdIt;
			}
			// Add Hi-Speed
			if (shrinkTick != prevTick)
			{
				for (auto&& [layerID, layer] : updatingLayers)
				{
					float currSpeed = 1.0f;
					if (layer.hiSpeedChanges.size())
					{
						// Using lower bound here because we went the speed before the current tick
						auto it = layer.hiSpeedChanges.lower_bound(shrinkTick);
						if (it == layer.hiSpeedChanges.begin())
							currSpeed = it->second.speed;
						else
							currSpeed = std::prev(it)->second.speed;
					}

					auto& hiSpeedChanges = score.layers[layerID].hiSpeedChanges;
					float shrinkSpeed = (shrinkTick - prevTick) * currSpeed;
					auto it = hiSpeedChanges.upper_bound(tick - 1);
					if (it == hiSpeedChanges.begin() ||
					    (std::prev(it)->second.speed != shrinkSpeed &&
					     std::prev(it)->first != tick - 1))
						hiSpeedChanges.emplace(tick - 1, HiSpeed{ tick - 1, layerID, shrinkSpeed });
					else
						std::prev(it)->second.speed = shrinkSpeed;
					selection.hispeeds.emplace(layerID, tick - 1);
				}
			}
			prevTick = shrinkTick;
		}

		for (auto&& [layerID, layer] : updatingLayers)
		{
			float endSpeed = 1.0f;
			if (layer.hiSpeedChanges.size())
			{
				auto it = layer.hiSpeedChanges.upper_bound(prevTick);
				if (it == layer.hiSpeedChanges.begin())
					endSpeed = it->second.speed;
				else
					endSpeed = std::prev(it)->second.speed;
			}

			auto& hiSpeedChanges = score.layers[layerID].hiSpeedChanges;
			auto it = hiSpeedChanges.upper_bound(tick);
			if (it == hiSpeedChanges.begin() ||
			    (std::prev(it)->second.speed != endSpeed && std::prev(it)->first != tick))
				hiSpeedChanges.emplace(tick, HiSpeed{ tick, layerID, endSpeed });
			else
				std::prev(it)->second.speed = endSpeed;
			selection.hispeeds.emplace(layerID, tick);
		}

		for (const auto& holdID : updatingHold)
			score.holdNotes.at(holdID).sortSteps(score.notes, !metadata.isExtendedScore);

		pushHistory("Compress selection");
	}

	void ScoreContext::connectHoldsInSelection()
	{
		if (selectedNotes.size() != 2)
			return;
		const Note& n1 = *selectedNotes.begin()->second;
		const Note& n2 = *(++selectedNotes.begin())->second;
		if (!n1.isHold() || !n2.isHold())
			return;
		if (n1.tick == n2.tick)
		{
			const HoldNote& h1 = score.holdNotes.at(n1.holdID);
			const HoldNote& h2 = score.holdNotes.at(n2.holdID);
			if (n2.ID == h2.steps.front() && n1.ID == h1.steps.back())
				connectHolds(n1.holdID, n2.holdID);
			else
				connectHolds(n2.holdID, n1.holdID);
		}
		else if (n1.tick < n2.tick)
			connectHolds(n1.holdID, n2.holdID);
		else
			connectHolds(n2.holdID, n1.holdID);
	}

	void ScoreContext::splitHoldInSelection()
	{
		if (selectedNotes.size() != 1)
			return;
		const Note& note = *selectedNotes.begin()->second;
		HoldNote& hold = score.holdNotes.at(note.holdID);
		auto&& [oldID, newID] = splitHoldAt(
		    hold, std::distance(hold.steps.begin(),
		                        std::find(hold.steps.begin(), hold.steps.end(), note.ID)));
		if (!metadata.isExtendedScore)
		{
			HoldNote& oldHold = score.holdNotes.at(oldID);
			Note& oldNote = score.notes.at(oldHold.steps.back());
			oldNote.flag = setFlag(oldNote.flag, NoteFlag::Attached | NoteFlag::NonAttached, false);
			HoldNote& newHold = score.holdNotes.at(newID);
			Note& newNote = score.notes.at(newHold.steps.front());
			newNote.flag = setFlag(newNote.flag, NoteFlag::Attached | NoteFlag::NonAttached, false);
		}
	}

	void ScoreContext::convertHoldToTraces(int quarterDivision, bool deleteHold, bool update)
	{
		if (selection.emptyNotes())
			return;

		std::unordered_map<const HoldNoteStep*, id_t> updatingHoldSteps;
		for (auto [_, note] : selectedNotes)
		{
			if (!note->isHold())
				continue;

			const HoldNote& hold = score.holdNotes.at(note->holdID);
			const HoldNoteStep& step = hold.holdStepAt(*note, score.notes);
			if (note->ID != step.ID)
				continue;
			updatingHoldSteps.emplace(&step, hold.ID);
		}

		clearNoteSelection();
		for (auto&& [pstep, holdID] : updatingHoldSteps)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			const HoldNote& constHold = hold;
			size_t nextSepIdx = std::distance(constHold.separators.data(), pstep) + 1;
			id_t startID = pstep->ID;
			id_t endID = nextSepIdx < constHold.separators.size() ? hold.separators[nextSepIdx].ID
			                                                      : hold.steps.back();
			const Note& holdStart = score.notes.at(startID);
			const Note& holdEnd = score.notes.at(endID);
			auto compare = HoldNote::StepIdComparer(score.notes);
			auto nextJoint =
			    std::upper_bound(hold.joints.begin(), hold.joints.end(), holdStart, compare);
			// Find the connector head and tail for each trace note
			auto itJoint = std::prev(nextJoint);
			bool critical =
			    pstep->isCrit() || (pstep->isGuide() && pstep->guideColor == GuideColor::Yellow);
			tick_t tickPerDivision = TICKS_PER_QUARTER / quarterDivision;
			tick_t startTick = (holdStart.tick / tickPerDivision) * tickPerDivision;
			if (startTick < holdStart.tick)
				startTick += tickPerDivision;
			for (tick_t tick = startTick; tick <= holdEnd.tick; tick += tickPerDivision)
			{
				// Do not create trace notes if they will overlap with the hold start or end
				if (!deleteHold)
				{
					if (tick == holdStart.tick &&
					    !hasFlag(holdStart.flag, NoteFlag::Hidden | NoteFlag::Trace))
						continue;
					if (tick == holdEnd.tick &&
					    !hasFlag(holdEnd.flag, NoteFlag::Hidden | NoteFlag::Trace))
						continue;
				}

				const Note *head = &score.notes.at(*itJoint), *tail = &score.notes.at(*nextJoint);
				while (tail->tick < tick)
				{
					++itJoint;
					++nextJoint;
					head = tail;
					tail = &score.notes.at(*nextJoint);
				}

				float percent = unlerp(head->tick, tail->tick, tick);
				auto easeFunc = getEaseFunction(head->ease);
				float left = easeFunc(head->lane, tail->lane, percent);
				float right = easeFunc(head->lane + head->width, tail->lane + tail->width, percent);
				// Insert a trace note
				Note newNote;
				newNote.flag = setFlag(newNote.flag, NoteFlag::Critical, critical);
				newNote.flag = setFlag(newNote.flag, NoteFlag::Trace);
				newNote.flag = setFlag(newNote.flag, NoteFlag::Attached, metadata.isExtendedScore);
				newNote.layer = holdStart.layer;
				newNote.tick = tick;
				newNote.lane = metadata.isExtendedScore ? left : std::round(left);
				newNote.width = metadata.isExtendedScore ? right - left : std::round(right - left);

				Note* inserted = insertNote(newNote, !deleteHold ? holdID : -1, false);
				if (inserted)
					selectNote(*inserted, false);
			}

			hold.updateJoints(score.notes);
			hold.updateLongs(score.notes);
			hold.updateFading(score.notes);

			if (deleteHold)
			{
				if (hold.separators.size() <= 1)
					eraseHold(hold, false);
				else if (hold.separators.data() == pstep)
				{
					size_t index = std::distance(
					    hold.steps.begin(), std::find(hold.steps.begin(), hold.steps.end(), endID));
					splitHoldAt(hold, index, false);
					eraseHold(hold, false);
				}
				else
				{
					size_t index = std::distance(
					    hold.steps.begin(), std::find(hold.steps.begin(), hold.steps.end(), endID));
					if (index > 0 && index < hold.steps.size() - 1)
						splitHoldAt(hold, index, false);
					index = std::distance(hold.steps.begin(),
					                      std::find(hold.steps.begin(), hold.steps.end(), startID));
					auto&& [_, delHoldID] = splitHoldAt(hold, index, false);
					eraseHold(score.holdNotes.at(delHoldID), false);
				}
			}
		}

		if (updatingHoldSteps.size())
		{
			updateSelectionFlag();
			pushHistory("Convert slides into traces");
		}
	}

	void ScoreContext::lerpHiSpeeds(int quarterDivision, EaseType ease)
	{
		if (selection.hispeeds.size() < 2)
			return;

		std::vector<layered_tick_t> insertedHispeed;
		const tick_t tickPerDivision = TICKS_PER_QUARTER / quarterDivision;
		const EaseFunction easeFunc = getEaseFunction(ease);
		constexpr tick_t MIN_TICK = 0;
		for (auto it = selection.hispeeds.begin(); it != selection.hispeeds.end();)
		{
			// Find the range of hispeeds in the same layer
			auto&& [layer, _] = *it;
			auto first = selection.hispeeds.lower_bound({ layer, MIN_TICK });
			auto last = selection.hispeeds.upper_bound({ layer, MAX_TICK });
			auto& hiSpeedChanges = score.layers[layer].hiSpeedChanges;

			for (auto next = std::next(first); next != last; first = next++)
			{
				const HiSpeed& hispeed = hiSpeedChanges.at(it->second);
				const HiSpeed& nxtHispeed = hiSpeedChanges.at(next->second);
				for (tick_t tick =
				         (hispeed.tick + tickPerDivision) / tickPerDivision * tickPerDivision;
				     tick < nxtHispeed.tick; tick += tickPerDivision)
				{
					float speed = easeFunc(hispeed.speed, nxtHispeed.speed,
					                       unlerp(hispeed.tick, nxtHispeed.tick, tick));
					auto insertIt = hiSpeedChanges.lower_bound(tick);
					if (insertIt == hiSpeedChanges.end() || insertIt->second.tick != tick)
						hiSpeedChanges.emplace_hint(insertIt, tick, HiSpeed{ tick, layer, speed });
					else
						insertIt->second.speed = speed;
					insertedHispeed.emplace_back(layer, tick);
				}
			}

			it = last;
		}

		selection.hispeeds.insert<std::vector<layered_tick_t>::iterator>(insertedHispeed.begin(),
		                                                                 insertedHispeed.end());

		if (insertedHispeed.size())
		{
			updateSelectionFlag();
			pushHistory("Lerp hispeeds");
		}
	}

	void ScoreContext::convertHoldToGuide(GuideColor color)
	{
		if (selection.emptyNotes())
			return;

		if (!metadata.isExtendedScore)
			color = color == GuideColor::Yellow ? color : GuideColor::Green;

		bool edit = false;
		std::unordered_map<HoldNoteStep*, id_t> updatingHoldSteps;
		for (auto [_, note] : selectedNotes)
		{
			if (!note->isHold())
				continue;

			HoldNote& hold = score.holdNotes.at(note->holdID);
			HoldNoteStep& step = hold.holdStepAt(*note, score.notes);
			if (step.ID != note->ID)
				continue;
			updatingHoldSteps.emplace(&step, hold.ID);
		}

		for (auto&& [pstep, holdID] : updatingHoldSteps)
		{
			edit |= !pstep->isGuide() || pstep->guideColor != color;
			pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Guide);
			pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Critical, false);
			pstep->guideColor = color;

			HoldNote& hold = score.holdNotes.at(holdID);
			if (!metadata.isExtendedScore)
			{
				for (auto stepID : hold.steps)
				{
					Note& note = score.notes.at(stepID);
					note.flag = setFlag(note.flag, NoteFlag::Hidden);
				}
				hold.separators.front().layer = HoldStepLayer::Bottom;
			}
			hold.updateJoints(score.notes);
			hold.updateLongs(score.notes);
			hold.updateFading(score.notes);
		}

		if (edit)
		{
			updateSelectionFlag();
			pushHistory("Convert hold to guide");
		}
	}

	void ScoreContext::convertGuideToHold(bool critical)
	{
		if (selection.emptyNotes())
			return;

		bool edit = false;
		std::unordered_map<HoldNoteStep*, id_t> updatingHoldSteps;
		for (auto [_, note] : selectedNotes)
		{
			if (!note->isHold())
				continue;

			HoldNote& hold = score.holdNotes.at(note->holdID);
			HoldNoteStep& step = hold.holdStepAt(*note, score.notes);
			if (step.ID != note->ID)
				continue;
			updatingHoldSteps.emplace(&step, hold.ID);
		}

		for (auto&& [pstep, holdID] : updatingHoldSteps)
		{
			edit |= pstep->isGuide() || pstep->isCrit() != critical;
			pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Guide, false);
			pstep->flag = setFlag(pstep->flag, HoldNoteFlag::Critical, critical);

			HoldNote& hold = score.holdNotes.at(holdID);
			hold.updateLongs(score.notes);
			if (!metadata.isExtendedScore)
			{
				for (auto stepID : hold.steps)
				{
					Note& note = score.notes.at(stepID);
					note.flag = setFlag(note.flag, NoteFlag::Critical, critical);
				}
				hold.separators.front().layer = HoldStepLayer::Top;
			}
		}

		if (edit)
		{
			updateSelectionFlag();
			pushHistory("Convert guide to hold");
		}
	}

	void ScoreContext::convertHoldToNone()
	{
		if (selection.emptyNotes() || !metadata.isExtendedScore)
			return;

		std::unordered_map<const HoldNoteStep*, id_t> updatingHoldSteps;
		for (auto [_, note] : selectedNotes)
		{
			if (!note->isHold())
				continue;

			const HoldNote& hold = score.holdNotes.at(note->holdID);
			const HoldNoteStep& step = hold.holdStepAt(*note, score.notes);
			if (step.ID != note->ID)
				continue;
			updatingHoldSteps.emplace(&step, hold.ID);
		}

		clearNoteSelection();
		for (auto&& [pstep, holdID] : updatingHoldSteps)
		{
			// Erase duplicate note cause by splitting
			bool eraseStart = false, eraseEnd = false;
			HoldNote& hold = score.holdNotes.at(holdID);
			// Split the hold from it's current chain (if exist)
			if (hold.separators.size() > 1)
			{
				if (hold.separators.data() == pstep)
				{
					size_t index = std::distance(
					    hold.steps.begin(),
					    std::find(hold.steps.begin(), hold.steps.end(), hold.separators[1].ID));
					id_t otherID = splitHoldAt(hold, index, false).second;
					eraseEnd = true;
					HoldNote& otherHold = score.holdNotes.at(otherID);
					otherHold.updateLongs(score.notes);
					otherHold.updateFading(score.notes);
				}
				else
				{
					size_t nextSepIdx =
					    std::distance<const HoldNoteStep*>(hold.separators.data(), pstep) + 1;
					id_t endID = nextSepIdx < hold.separators.size()
					                 ? hold.separators[nextSepIdx].ID
					                 : hold.steps.back();

					id_t otherID;
					size_t index = std::distance(
					    hold.steps.begin(), std::find(hold.steps.begin(), hold.steps.end(), endID));
					if (index > 0 && index < hold.steps.size() - 1)
					{
						otherID = splitHoldAt(hold, index, false).second;
						eraseEnd = true;
						HoldNote& otherHold = score.holdNotes.at(otherID);
						otherHold.updateLongs(score.notes);
						otherHold.updateFading(score.notes);
					}
					index =
					    std::distance(hold.steps.begin(),
					                  std::find(hold.steps.begin(), hold.steps.end(), pstep->ID));
					std::tie(otherID, holdID) = splitHoldAt(hold, index, false);
					eraseStart = true;
					HoldNote& otherHold = score.holdNotes.at(otherID);
					otherHold.updateLongs(score.notes);
					otherHold.updateFading(score.notes);
				}
			}

			HoldNote& erasingHold = holdID == hold.ID ? hold : score.holdNotes.at(holdID);
			if (eraseStart)
			{
				eraseNote(score.notes.at(erasingHold.steps.front()), false);
				erasingHold.steps.erase(erasingHold.steps.begin());
			}
			if (eraseEnd)
			{
				eraseNote(score.notes.at(erasingHold.steps.back()), false);
				erasingHold.steps.pop_back();
			}
			for (const auto& step : erasingHold.steps)
			{
				Note& note = score.notes.at(step);
				note.holdID = -1;
				note.flag = setFlag(note.flag, NoteFlag::LongNote, false);
				note.flag = setFlag(note.flag, NoteFlag::NonAttached,
				                    hasFlag(note.flag, NoteFlag::Attached));
			}
			score.holdNotes.erase(erasingHold.ID);
		}

		if (updatingHoldSteps.size())
		{
			updateSelectionFlag();
			pushHistory("Convert slides into none");
		}
	}

	void ScoreContext::updateViews()
	{
		hoveringNotes.clear();
		notesOrderedView.clear();

		for (auto&& [ID, note] : score.notes)
		{
			notesOrderedView.emplace(note.tick, &note);
		}

		waypointOrderedView.clear();
		for (auto&& [ID, waypoint] : score.waypoints)
		{
			waypointOrderedView.emplace(waypoint.tick, &waypoint);
		}
	}

	HistoryContext ScoreContext::historyContext()
	{
		return HistoryContext{ score, metadata, selection };
	}

	void ScoreContext::undo()
	{
		if (!history.hasUndo())
			return;

		HistoryContext ctx = historyContext();
		history.undo(ctx);
		onHistoryApplied();
	}

	void ScoreContext::redo()
	{
		if (!history.hasRedo())
			return;

		HistoryContext ctx = historyContext();
		history.redo(ctx);
		onHistoryApplied();
	}

	void ScoreContext::onHistoryPushed(bool invalidateCapture) {}

	// Rebuild everything derived from the persistent state after an undo/redo
	void ScoreContext::onHistoryApplied()
	{
		isPendingLoadMusic |= workingMetadata.musicFile != metadata.musicFile;
		isPendingChangeMusicOffset |= workingMetadata.musicOffset != metadata.musicOffset;

		workingMetadata = metadata;
		selectedLayer = std::clamp<id_t>(selectedLayer, 0, score.layers.size() - 1);
		upToDate = recentHistoryUndo == history.undoCount();
		updateViews();
		updateSelectionView();
		updateSelectionFlag();

		scoreStats.calculateStats(score);
	}

	// TODO(history-migration): legacy call sites. They no longer record anything in the
	// history (undo/redo does not cover them) until they are migrated to the typed overload.
	void ScoreContext::pushHistory(std::string_view) { onHistoryPushed(); }

	void ScoreContext::pushHistory(std::string_view description, HistoryEdit edit,
	                               SelectionChange selectionChange)
	{
		history.pushHistory(
		    History{ std::string(description), std::move(edit), std::move(selectionChange) });
		onHistoryPushed();
	}

	SelectionRef ScoreContext::getSelectionSnapshot()
	{
		if (selection.empty())
			return nullptr;
		// Reuse the previous snapshot while the selection is unchanged, so a chain of edits on
		// the same selection all share one object
		if (!selectionSnapshot || !(*selectionSnapshot == selection))
			selectionSnapshot = std::make_shared<const ScoreSelection>(selection);
		return selectionSnapshot;
	}

	void ScoreContext::pushEdit(std::string_view description, HistoryEdit edit, bool saveSelection)
	{
		SelectionChange selectionChange;
		if (saveSelection)
		{
			SelectionRef snapshot = getSelectionSnapshot();
			// The edit does not change what is selected
			selectionChange = SelectionChange{ snapshot, snapshot };
		}
		pushHistory(description, std::move(edit), std::move(selectionChange));
	}

	void ScoreContext::pushEdit(std::string_view description, HistoryEdit edit,
	                            SelectionRef&& selectionBefore)
	{
		pushHistory(description, std::move(edit), { selectionBefore, getSelectionSnapshot() });
	}

	bool ScoreContext::pushSelectionChanged(std::string_view description)
	{
		auto edit = selectionCapture.diffChanges(score);
		if (!edit)
			return false;

		pushEdit(description, toHistoryEdit(std::move(*edit)), true);
		return true;
	}

	bool ScoreContext::pushSelectionEdit(std::string_view description, ScoreCapture&& baseCapture,
	                                     SelectionRef&& selectionBefore)
	{
		updateSelectionCapture();
		auto edit = baseCapture.diffAll(selectionCapture);
		if (!edit)
			return false;

		history.pushHistory(History{ std::string(description),
		                             std::move(*edit),
		                             { std::move(selectionBefore), getSelectionSnapshot() } });
		onHistoryPushed(false); // Already validated before pushed
		return true;
	}

	Note* ScoreContext::insertNote(const Note& note, id_t holdID, bool update, bool select)
	{
		if (!metadata.isExtendedScore)
		{
			switch (note.type)
			{
			case NoteType::Tap:
				if (holdID >= 0)
					holdID = -1;
				if (note.isHidden() || note.isDummy())
					return nullptr;
				break;
			case NoteType::Tick:
				if (holdID < 0 || note.isDummy())
					return nullptr;
				break;
			case NoteType::Damage:
			default:
				return nullptr;
			}
		}

		ScoreCapture captureBefore;
		SelectionRef selectionBefore;
		const id_t beforeNoteID = nextNoteID;
		if (update)
		{
			if (holdID >= 0)
				captureBefore.captureHold(score, holdID);
			selectionBefore = getSelectionSnapshot();
		}

		id_t nextID = nextNoteID++;
		Note& newNote = score.notes.emplace(nextID, note).first->second;
		newNote.ID = nextID;
		newNote.holdID = -1;
		if (!metadata.isExtendedScore)
		{
			newNote.width = std::floor(newNote.width + std::modf(newNote.lane, &newNote.lane));
			newNote.soundEffect = SoundEffectType::Default;
		}
		newNote.width = std::clamp(newNote.width, minNoteWidth(), maxNoteWidth());
		newNote.lane = std::clamp(newNote.lane, minLane(), maxLane(newNote.width));
		newNote.layer = selectedLayer;
		newNote.ease = newNote.ease >= maxEase() ? EaseType::Linear : newNote.ease;
		newNote.flick = newNote.flick >= maxFlick() ? FlickType::Default : newNote.flick;
		if (newNote.isHidden())
			newNote.flag = setFlag(newNote.flag, NoteFlag::Dummy, false);

		notesOrderedView.emplace(newNote.tick, &newNote);

		if (holdID >= 0)
		{
			HoldNote& hold = score.holdNotes.at(holdID);
			if (!metadata.isExtendedScore && hold.separators.front().isGuide())
			{
				newNote.flag = setFlag(newNote.flag, NoteFlag::Hidden);
				newNote.flag = setFlag(newNote.flag, NoteFlag::Attached, false);
			}
			hold.insertStep(newNote, score.notes, !metadata.isExtendedScore, update);
		}
		else
			newNote.flag = setFlag(newNote.flag, NoteFlag::Attached, false);

		if (update)
		{
			if (select)
				selectNote(newNote, true);

			ScoreCapture captureAfter;
			captureAfter.captureNote(score, newNote.ID);
			if (holdID >= 0)
				captureAfter.captureHold(score, holdID);
			if (auto edit = captureBefore.diffAll(captureAfter))
				pushEdit("Insert note", toHistoryEdit(std::move(*edit)),
				         std::move(selectionBefore));
		}
		return &newNote;
	}

	void ScoreContext::eraseNote(Note& note, bool update)
	{
		auto it = score.notes.find(note.ID);
		if (it == score.notes.end())
			return;
		id_t noteID = note.ID;

		SingleNoteEdit edit{ noteID, note, std::nullopt };
		SelectionRef selectionBefore = update ? getSelectionSnapshot() : nullptr;
		for (auto&& [it, end] = notesOrderedView.equal_range(note.tick); it != end; ++it)
		{
			if (it->second != &note)
				continue;
			notesOrderedView.erase(it);
			break;
		}
		score.notes.erase(it);
		bool selectionChanged = selection.notes.erase(noteID);
		selectedNotes.erase(noteID);

		if (update)
		{
			if (selectionChanged)
				updateSelectionFlag();
			pushEdit("Erase note", edit, std::move(selectionBefore));
		}
	}

	std::tuple<HoldNote&, Note&, Note&> ScoreContext::insertHold(const Note& start, const Note& end,
	                                                             const HoldNote& hold, bool update,
	                                                             bool select)
	{
		SelectionRef selectionBefore = update ? getSelectionSnapshot() : nullptr;

		id_t nextIDH = nextHoldID++;

		id_t nextID = nextNoteID++;
		Note& holdStart = score.notes.emplace(nextID, start).first->second;
		holdStart.ID = nextID;
		holdStart.holdID = nextIDH;
		holdStart.width = !metadata.isExtendedScore ? std::floor(holdStart.width) : holdStart.width;
		holdStart.width = std::clamp(holdStart.width, minNoteWidth(), maxNoteWidth());
		holdStart.lane = !metadata.isExtendedScore ? std::floor(holdStart.lane) : holdStart.lane;
		holdStart.lane = std::clamp(holdStart.lane, minLane(), maxLane(holdStart.width));
		holdStart.layer = selectedLayer;
		holdStart.flag = setFlag(holdStart.flag, NoteFlag::LongNote);
		holdStart.flag = setFlag(holdStart.flag, NoteFlag::NonAttached,
		                         hasFlag(holdStart.flag, NoteFlag::Attached));
		holdStart.ease = holdStart.ease >= maxEase() ? EaseType::Linear : holdStart.ease;

		if (!metadata.isExtendedScore)
		{
			holdStart.flag = setFlag(holdStart.flag, NoteFlag::Dummy, false);
			holdStart.flick = FlickType::None;
			holdStart.type = NoteType::Tap;
			if (hold.separators.front().isGuide())
				holdStart.flag = setFlag(holdStart.flag, NoteFlag::Hidden);
		}

		nextID = nextNoteID++;
		Note& holdEnd = score.notes.emplace(nextID, end).first->second;
		holdEnd.ID = nextID;
		holdEnd.holdID = nextIDH;
		holdEnd.width = !metadata.isExtendedScore ? std::floor(holdEnd.width) : holdEnd.width;
		holdEnd.width = std::clamp(holdEnd.width, minNoteWidth(), maxNoteWidth());
		holdEnd.lane = !metadata.isExtendedScore ? std::floor(holdEnd.lane) : holdEnd.lane;
		holdEnd.lane = std::clamp(holdEnd.lane, minLane(), maxLane(holdEnd.width));
		holdEnd.layer = selectedLayer;
		holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::LongNote);
		holdEnd.flag =
		    setFlag(holdEnd.flag, NoteFlag::NonAttached, hasFlag(holdEnd.flag, NoteFlag::Attached));
		holdEnd.ease = holdEnd.ease >= maxEase() ? EaseType::Linear : holdEnd.ease;

		if (!metadata.isExtendedScore)
		{
			holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::Dummy, false);
			holdEnd.type = NoteType::Tap;
			if (hold.separators.front().isGuide())
				holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::Hidden);
		}

		notesOrderedView.emplace(holdStart.tick, &holdStart);
		notesOrderedView.emplace(holdEnd.tick, &holdEnd);

		HoldNote& newHold = score.holdNotes[nextIDH];
		newHold.fadeType = hold.fadeType;
		newHold.ID = nextIDH;
		newHold.steps.clear();
		newHold.joints.clear();
		newHold.separators.clear();
		newHold.steps.push_back(holdStart.ID);
		newHold.steps.push_back(holdEnd.ID);
		newHold.joints.push_back(holdStart.ID);
		newHold.joints.push_back(holdEnd.ID);
		newHold.separators.push_back(hold.separators.front());

		HoldNoteStep& holdStep = newHold.separators.front();
		holdStep.ID = newHold.steps.front();
		if (!metadata.isExtendedScore)
		{
			holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Dummy, false);
			newHold.fadeType = FadeType::Classic;
			if (holdStep.guideColor != GuideColor::Yellow)
				holdStep.guideColor = GuideColor::Green;
			holdStep.layer = holdStep.isGuide() ? HoldStepLayer::Bottom : HoldStepLayer::Top;
		}

		if (update)
			newHold.updateFading(score.notes);

		if (update)
		{
			if (select)
			{
				selectNote(holdStart, false);
				selectNote(holdEnd, true);
			}

			ScoreCapture beforeCapture;
			ScoreCapture afterCapture;
			afterCapture.captureHold(score, newHold.ID);
			if (auto edit = beforeCapture.diffAll(afterCapture))
				pushEdit("Insert hold note", toHistoryEdit(std::move(*edit)),
				         std::move(selectionBefore));
		}

		return { newHold, holdStart, holdEnd };
	}

	void ScoreContext::eraseHold(HoldNote& hold, bool update)
	{
		ScoreCapture beforeCapture;
		SelectionRef selectionBefore;
		if (update)
		{
			beforeCapture.captureHold(score, hold.ID);
			selectionBefore = getSelectionSnapshot();
		}

		for (const auto& step : hold.steps)
			eraseNote(score.notes.at(step), false);
		score.holdNotes.erase(hold.ID);
		if (update)
		{
			updateSelectionFlag();
			if (auto edit = beforeCapture.diffAll(ScoreCapture()))
				pushEdit("Erase hold", toHistoryEdit(std::move(*edit)), std::move(selectionBefore));
		}
	}

	id_t ScoreContext::connectHolds(id_t currHoldID, id_t nextHoldID, bool update)
	{
		HoldNote& currHold = score.holdNotes.at(currHoldID);
		HoldNote& nextHold = score.holdNotes.at(nextHoldID);
		const HoldNoteStep& currStep = currHold.separators.back();
		const HoldNoteStep& laterStep = nextHold.separators.front();

		Note& earlierStartNote = score.notes.at(currHold.separators.back().ID);
		Note& earlierNote = score.notes.at(currHold.steps.back());
		Note& laterNote = score.notes.at(nextHold.steps.front());
		assert(earlierNote.tick <= laterNote.tick);

		bool startCrit = hasFlag(earlierStartNote.flag, NoteFlag::Critical);
		bool mergeNote = earlierNote.tick == laterNote.tick && earlierNote.lane == laterNote.lane &&
		                 earlierNote.width == laterNote.width;

		bool mergeHold = !metadata.isExtendedScore;
		if (currStep.isGuide() == false && laterStep.isGuide() == false)
			mergeHold |= currStep.isCrit() == laterStep.isCrit() &&
			             currStep.isDummy() == laterStep.isDummy();
		else if (currStep.isGuide() == true && laterStep.isGuide() == true)
			mergeHold |= currStep.guideColor == laterStep.guideColor;

		if (mergeNote)
		{
			eraseNote(earlierNote, false);
			currHold.steps.pop_back();
		}

		// Copy over latter hold steps
		for (auto& step : nextHold.steps)
		{
			currHold.steps.push_back(step);

			Note& note = score.notes.at(step);
			bool isLastStep = step == nextHold.steps.back();
			note.holdID = currHold.ID;

			if (!metadata.isExtendedScore)
			{
				if (!isLastStep)
					note.type = NoteType::Tick;
				bool isCrit = startCrit || isLastStep && (note.isFlick() || note.isTrace());
				note.flag = setFlag(note.flag, NoteFlag::Critical, isCrit);
				if (currStep.isGuide())
				{
					note.flag = setFlag(note.flag, NoteFlag::Hidden);
					note.flag = setFlag(note.flag, NoteFlag::Attached | NoteFlag::Dummy, false);
				}
			}
		}

		laterNote.flag = setFlag(laterNote.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);
		if (!mergeNote)
		{
			earlierNote.flag =
			    setFlag(earlierNote.flag, NoteFlag::LongNote | NoteFlag::NonAttached, false);
			if (!metadata.isExtendedScore)
			{
				earlierNote.type = NoteType::Tick;
				earlierNote.flag = setFlag(earlierNote.flag, NoteFlag::Critical, startCrit);
				if (currStep.isGuide())
				{
					earlierNote.flag = setFlag(earlierNote.flag, NoteFlag::Hidden);
					earlierNote.flag =
					    setFlag(earlierNote.flag, NoteFlag::Attached | NoteFlag::Dummy, false);
				}
			}
			if (HoldNote::StepComparer()(laterNote, earlierNote))
			{
				swapNotePosition(earlierNote, laterNote);
				swapNoteProperties(earlierNote, laterNote);
			}
		}
		else if (currHold.separators.back().ID == currHold.steps.back())
		{
			// The last step that merged was a separator, we need to delete the separator
			currHold.separators.pop_back();
		}
		auto sepStartIt = nextHold.separators.begin();
		if (!metadata.isExtendedScore ||
		    (nextHold.separators.size() > 1 && nextHold.separators[1].ID == sepStartIt->ID))
			++sepStartIt;
		currHold.separators.insert(currHold.separators.end(), sepStartIt,
		                           nextHold.separators.end());
		score.holdNotes.erase(nextHoldID);
		currHold.updateSeparators(score.notes);
		currHold.updateJoints(score.notes);
		currHold.updateLongs(score.notes);
		currHold.updateFading(score.notes);

		if (update)
		{
			deselectAll();
			pushHistory("Connect holds");
		}

		return currHoldID;
	}

	std::pair<id_t, id_t> ScoreContext::splitHoldAt(HoldNote& hold, size_t index, bool update)
	{
		assert(index > 0 && index < hold.steps.size() - 1);
		Note& holdEnd = score.notes.at(hold.steps[index]);

		auto separatorIt =
		    std::prev(std::upper_bound(hold.separators.begin(), hold.separators.end(), holdEnd,
		                               HoldNote::HoldStepComparer(score.notes)));
		HoldNoteStep& holdStep = *separatorIt;
		HoldNote& newHold = score.holdNotes[nextHoldID];
		newHold.fadeType = hold.fadeType;
		newHold.ID = nextHoldID++;
		newHold.steps.clear();
		newHold.joints.clear();
		newHold.separators.clear();

		if (!metadata.isExtendedScore)
		{
			holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Dummy, false);
			newHold.fadeType = FadeType::Classic;
			if (holdStep.guideColor != GuideColor::Yellow)
				holdStep.guideColor = GuideColor::Green;

			holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::Dummy, false);
			holdEnd.flick = FlickType::None;
			holdEnd.type = NoteType::Tap;
			holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::Critical,
			                       hasFlag(holdStep.flag, HoldNoteFlag::Critical));
			if (holdStep.isGuide())
				holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::Hidden);
		}
		holdEnd.flag = setFlag(holdEnd.flag, NoteFlag::LongNote);
		holdEnd.flag =
		    setFlag(holdEnd.flag, NoteFlag::NonAttached, hasFlag(holdEnd.flag, NoteFlag::Attached));
		Note& newHoldStart = score.notes[nextNoteID] = holdEnd;
		newHoldStart.ID = nextNoteID++;
		newHoldStart.holdID = newHold.ID;
		newHold.steps.push_back(newHoldStart.ID);

		notesOrderedView.emplace(newHoldStart.tick, &newHoldStart);

		for (size_t i = index + 1; i < hold.steps.size(); ++i)
		{
			Note& stepNote = score.notes.at(hold.steps[i]);
			stepNote.holdID = newHold.ID;
			newHold.steps.push_back(stepNote.ID);
		}
		hold.steps.erase(hold.steps.begin() + index + 1, hold.steps.end());
		newHold.separators.insert(newHold.separators.end(), separatorIt, hold.separators.end());
		bool erasePrev = separatorIt != hold.separators.begin() && holdStep.ID == holdEnd.ID;
		hold.separators.erase(erasePrev ? separatorIt : std::next(separatorIt),
		                      hold.separators.end());

		hold.updateSeparators(score.notes);
		hold.updateJoints(score.notes);
		hold.updateLongs(score.notes);
		hold.updateFading(score.notes);
		newHold.updateSeparators(score.notes);
		newHold.updateJoints(score.notes);
		newHold.updateLongs(score.notes);
		newHold.updateFading(score.notes);

		if (update)
		{
			clearSelection();
			selectNote(holdEnd, true);
			selectNote(newHoldStart, true);
			pushHistory("Split hold");
		}

		return { hold.ID, newHold.ID };
	}

	void ScoreContext::insertTempoChange(const Tempo& tempo)
	{
		auto&& [tempoIt, inserted] = score.tempoChanges.try_emplace(tempo.tick, tempo);
		std::optional<Tempo> before;
		if (!inserted)
		{
			auto&& [_, existedTempo] = *tempoIt;
			before = existedTempo;
			inserted = existedTempo.quarterPerMinute != tempo.quarterPerMinute;
			existedTempo.quarterPerMinute = tempo.quarterPerMinute;
		}
		if (inserted)
			pushEdit("Insert BPM change", SingleTempoEdit{ tempo.tick, before, tempoIt->second });
	}

	void ScoreContext::insertTimeSignature(const TimeSignature& timeSig)
	{
		auto&& [timeSigIt, inserted] = score.timeSignatures.try_emplace(timeSig.measure, timeSig);
		std::optional<TimeSignature> before;
		if (!inserted)
		{
			auto&& [_, existedTS] = *timeSigIt;
			before = existedTS;
			inserted |= existedTS.numerator != timeSig.numerator;
			inserted |= existedTS.denominator != timeSig.denominator;
			existedTS.numerator = timeSig.numerator;
			existedTS.denominator = timeSig.denominator;
		}
		if (inserted)
			pushEdit("Insert time signature",
			         SingleTimeSignatureEdit{ timeSig.measure, before, timeSigIt->second });
	}

	HiSpeed& ScoreContext::insertHispeedChange(const HiSpeed& hispeed, bool update)
	{
		HiSpeed newHispeed = hispeed;
		newHispeed.layer = selectedLayer;
		if (!metadata.isExtendedScore)
		{
			newHispeed.skips = 0;
			newHispeed.ease = HiSpeedEaseType::None;
			newHispeed.hideNotes = false;
		}
		auto& hispeedChanges = score.layers[selectedLayer].hiSpeedChanges;
		auto&& [hispeedIt, inserted] = hispeedChanges.try_emplace(hispeed.tick, newHispeed);
		std::optional<HiSpeed> before;
		if (!inserted)
		{
			auto&& [_, existedHspd] = *hispeedIt;
			before = existedHspd;
			inserted |= existedHspd.speed != newHispeed.speed;
			inserted |= existedHspd.skips != newHispeed.skips;
			inserted |= existedHspd.ease != newHispeed.ease;
			inserted |= existedHspd.hideNotes != newHispeed.hideNotes;
			existedHspd = newHispeed;
		}
		if (inserted && update)
			pushEdit("Insert hi-speed changes",
			         SingleHiSpeedEdit{ layered_tick_t{ selectedLayer, hispeed.tick }, before,
			                            hispeedIt->second });
		return hispeedIt->second;
	}

	Waypoint& ScoreContext::insertWaypoint(tick_t tick, const std::string& name)
	{
		id_t nextID = score.waypoints.size() ? score.waypoints.rbegin()->first + 1 : 0;
		auto&& [_, waypoint] = *score.waypoints.emplace_hint(score.waypoints.end(), nextID,
		                                                     Waypoint{ nextID, tick, name });
		waypointOrderedView.emplace(tick, &waypoint);
		pushEdit("Create new waypoint", SingleWaypointEdit{ nextID, std::nullopt, waypoint });
		return waypoint;
	}

	void ScoreContext::eraseWaypoint(id_t waypointID)
	{
		auto it = score.waypoints.find(waypointID);
		if (it == score.waypoints.end())
			return;
		const Waypoint before = it->second;
		const Waypoint* erasing = &it->second;
		for (auto viewIt = waypointOrderedView.begin(); viewIt != waypointOrderedView.end();
		     ++viewIt)
		{
			if (viewIt->second == erasing)
			{
				waypointOrderedView.erase(viewIt);
				break;
			}
		}
		score.waypoints.erase(it);
		pushEdit("Remove waypoint", SingleWaypointEdit{ waypointID, before, std::nullopt });
	}

	void ScoreContext::insertSkill(tick_t tick)
	{
		auto&& [skillIt, inserted] = score.skills.insert(Skill{ tick });
		if (inserted)
			pushEdit("Insert skill", SingleSkillEdit{ tick, std::nullopt, *skillIt });
	}

	void ScoreContext::setLaneExtension(int value, bool update)
	{
		if (metadata.laneExtension == value)
			return;
		metadata.laneExtension = value;
		// TODO(history-migration): remove when lane extension is migrated to pushWorkingMetadata
		workingMetadata.laneExtension = value;
		std::unordered_set<id_t> updatingHolds;
		// Clamp note within the extended lanes
		for (auto& [_, note] : score.notes)
		{
			// We'll try to keep the lane and width relative to the old values
			bool updated = false;
			if (note.lane < minLane())
			{
				note.lane += std::ceil(minLane() - note.lane);
				updated = true;
			}
			else if (note.lane > maxLane(note.width))
			{
				note.lane -= std::ceil(note.lane - maxLane(note.width));
			}
			if (note.width > maxNoteWidth(note.lane))
			{
				note.width -= std::ceil(note.width - maxNoteWidth(note.lane));
				updated = true;
			}
			if (updated && note.isHold())
				updatingHolds.insert(note.holdID);
		}
		for (auto& id : updatingHolds)
		{
			HoldNote& hold = score.holdNotes.at(id);
			hold.sortSteps(score.notes, !metadata.isExtendedScore);
		}

		if (update)
			pushHistory("Change lane extension");
	}

	void ScoreContext::setScoreExtension(bool isExtended)
	{
		if (metadata.isExtendedScore == isExtended)
			return;
		metadata.isExtendedScore = isExtended;
		if (!isExtended)
		{
			setLaneExtension(0, false);
			std::vector<Note> newNotes;
			for (auto& [_, note] : score.notes)
			{
				// Clamp note position
				if (!isDivisibleBy(note.lane, 1.f))
					note.lane = roundUnderHalf(note.lane);
				if (!isDivisibleBy(note.width, 1.f))
					note.width = std::round(note.width);
				note.width = std::clamp(note.width, minNoteWidth(), maxNoteWidth());
				note.lane = std::clamp(note.lane, minLane(), maxLane(note.width));
				// Unset extended features
				note.layer = 0;
				note.soundEffect = SoundEffectType::Default;
				note.ease = note.ease < maxEase() ? note.ease : EaseType::Linear;
				note.flick = note.flick < maxFlick() ? note.flick : FlickType::Default;
				constexpr NoteFlag extendedFlag =
				    NoteFlag::Dummy | NoteFlag::NonAttached | NoteFlag::LongNote;
				if (!note.isHold())
				{
					note.type = NoteType::Tap;
					note.flag = setFlag(
					    note.flag, NoteFlag::Hidden | NoteFlag::Attached | extendedFlag, false);
					if (note.type == NoteType::Damage)
						eraseNote(note, false);
				}
				else
				{
					note.flag = setFlag(note.flag, extendedFlag, false);
				}
			}
			for (auto&& [_, hold] : score.holdNotes)
			{
				hold.separators.erase(std::next(hold.separators.begin()), hold.separators.end());
				HoldNoteStep& holdStep = hold.separators.front();
				holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Dummy, false);
				hold.fadeType = FadeType::Classic;
				if (holdStep.guideColor != GuideColor::Yellow)
					holdStep.guideColor = GuideColor::Green;
				holdStep.layer = holdStep.isGuide() ? HoldStepLayer::Bottom : HoldStepLayer::Top;

				Note& startNote = score.notes.at(hold.steps.front());
				Note& endNote = score.notes.at(hold.steps.back());
				if (holdStep.isGuide())
				{
					holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Critical, false);
					if (startNote.type == NoteType::Tap && !startNote.isHidden())
					{
						newNotes.emplace_back(startNote).holdID = -1;
						startNote.tick += 1; // auto shrink up
					}
					if (endNote.type == NoteType::Tap && !endNote.isHidden())
					{
						newNotes.emplace_back(endNote).holdID = -1;
						endNote.tick -= 1; // auto shrink down
					}
					startNote.type = NoteType::Tap;
					startNote.flag = setFlag(startNote.flag, NoteFlag::Hidden);
					endNote.flag = setFlag(endNote.flag, NoteFlag::Hidden);

					startNote.flag = setFlag(startNote.flag, NoteFlag::Critical,
					                         holdStep.guideColor == GuideColor::Yellow);
					endNote.flag = setFlag(endNote.flag, NoteFlag::Critical, startNote.flag);

					for (size_t i = 1; i < hold.steps.size() - 1; ++i)
					{
						Note& stepNote = score.notes.at(hold.steps[i]);
						if (stepNote.isAttached())
						{
							auto startJoint = hold.jointBeforeStep(stepNote, score.notes);
							auto endJoint = hold.jointAfterStep(stepNote, score.notes);
							assert(startJoint != nullptr && endJoint != nullptr);
							if (startJoint && endJoint)
							{
								float l1 = startJoint->lane, r1 = l1 + startJoint->width;
								float l2 = endJoint->lane, r2 = l2 + endJoint->width;
								float ratio =
								    unlerp(startJoint->tick, endJoint->tick, stepNote.tick, 0.5f);
								auto easeFunc = getEaseFunction(startJoint->ease);
								float lane = easeFunc(l1, l2, ratio);
								float width = easeFunc(r1, r2, ratio) - lane;
								stepNote.width =
								    std::max(std::floor(std::modf(lane, &stepNote.lane) + width),
								             minNoteWidth());
							}
							stepNote.flag = setFlag(stepNote.flag, NoteFlag::Attached, false);
							newNotes.emplace_back(stepNote).holdID = -1;
						}
						else if (!stepNote.isHidden() || stepNote.type != NoteType::Tick)
						{
							newNotes.emplace_back(stepNote).holdID = -1;
						}
						stepNote.type = NoteType::Tick;
						stepNote.flag = setFlag(stepNote.flag, NoteFlag::Hidden);
						stepNote.flag = setFlag(stepNote.flag, NoteFlag::Critical, startNote.flag);
					}
				}
				else
				{
					holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Critical,
					                        hasFlag(startNote.flag, NoteFlag::Critical));
					if (!startNote.isHidden() && startNote.isFlick())
					{
						newNotes.emplace_back(startNote).holdID = -1;
						startNote.tick += 1; // auto shrink up
						startNote.flick = FlickType::None;
						startNote.flag = setFlag(startNote.flag, NoteFlag::Hidden);
					}
					if (!endNote.isTrace() && !endNote.isFlick())
						endNote.flag = setFlag(endNote.flag, NoteFlag::Critical, startNote.flag);

					for (size_t i = 1; i < hold.steps.size() - 1; ++i)
					{
						Note& stepNote = score.notes.at(hold.steps[i]);
						if (stepNote.isAttached() && stepNote.type != NoteType::Tick)
						{
							auto startJoint = hold.jointBeforeStep(stepNote, score.notes);
							auto endJoint = hold.jointAfterStep(stepNote, score.notes);
							assert(startJoint != nullptr && endJoint != nullptr);
							if (startJoint && endJoint)
							{
								float l1 = startJoint->lane, r1 = l1 + startJoint->width;
								float l2 = endJoint->lane, r2 = l2 + endJoint->width;
								float ratio =
								    unlerp(startJoint->tick, endJoint->tick, stepNote.tick, 0.5f);
								auto easeFunc = getEaseFunction(startJoint->ease);
								float lane = easeFunc(l1, l2, ratio);
								float width = easeFunc(r1, r2, ratio) - lane;
								stepNote.width =
								    std::max(std::floor(std::modf(lane, &stepNote.lane) + width),
								             minNoteWidth());
							}
							stepNote.flag = setFlag(stepNote.flag, NoteFlag::Attached, false);
							newNotes.emplace_back(stepNote).holdID = -1;

							stepNote.flag = setFlag(stepNote.flag, NoteFlag::Hidden);
						}
						else if (stepNote.type != NoteType::Tick && !stepNote.isHidden())
						{
							newNotes.emplace_back(stepNote).holdID = -1;
							stepNote.flag = setFlag(stepNote.flag, NoteFlag::Hidden);
						}

						stepNote.type = NoteType::Tick;
						stepNote.flag = setFlag(stepNote.flag, NoteFlag::Critical, startNote.flag);
					}
				}

				hold.sortSteps(score.notes, !metadata.isExtendedScore);
			}
			for (auto&& note : newNotes)
				insertNote(note, note.holdID, false);
			// Remove extra layers
			score.layers.erase(score.layers.begin() + 1, score.layers.end());
			score.layers[0].forceNoteSpeed = 0;
			selectedLayer = 0;
			for (auto&& [_, hispeed] : score.layers[0].hiSpeedChanges)
			{
				hispeed.skips = 0;
				hispeed.ease = HiSpeedEaseType::None;
				hispeed.hideNotes = false;
			}
		}

		pushHistory("Change score extension");
	}

	bool ScoreContext::isLayerVisible(id_t layer) const
	{
		return showAllLayers || !score.layers[layer].hidden;
	}

	bool ScoreContext::isLayerInteractive(id_t layer) const
	{
		return showAllLayers || layer == selectedLayer;
	}

	bool ScoreContext::isLayerSelected(id_t layer) const { return layer == selectedLayer; }
}

#include "SusSerializer.h"
#include "SUS.h"
#include "SusExporter.h"
#include "SusParser.h"
#include "Localization.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

static constexpr int MIN_LANE = 0;
static constexpr int MAX_LANE = 11;

namespace MikuMikuWorld
{
	void SusSerializer::serialize(const SerializingScore& data, std::string filename)
	{
		SUS sus = scoreToSus(data);

		SusExporter exporter{};
		exporter.dump(sus, filename, exportComment);
	}

	SerializingScore SusSerializer::deserialize(std::string filename)
	{
		SUS sus = SusParser().parse(filename);
		return susToScore(sus);
	}

	Result SusSerializer::canSerialize(const SerializingScore& data)
	{
		if (data.metadata.isExtendedScore)
			return Result(ResultStatus::Error, localize(Text::exportFormatNotAvailExtend).string);
		return Result::Ok();
	}

	static SUSNote toSUSNote(const Note& note, int type)
	{
		int lane = std::clamp<int>(note.lane, MIN_LANE, MAX_LANE);
		int width = std::clamp<int>(note.width, 1, 12 - lane);
		return SUSNote{ note.tick, lane + 2, width, type };
	}

	static Note fromSUSNote(const SUSNote& note, NoteType type)
	{
		Note n;
		n.tick = note.tick;
		n.lane = note.lane - 2;
		n.width = note.width;
		n.type = type;
		return n;
	}

	static std::pair<int, int> barLengthToFraction(float length, float fractionDenom)
	{
		int factor = 1;
		for (int i = 2; i < 10; ++i)
		{
			if (fmodf(factor * length, 1) == 0)
				return std::make_pair<int, int>(factor * length, std::pow(2, i));

			factor *= 2;
		}

		return std::pair<int, int>(4, 4);
	}

	static std::string noteKey(const SUSNote& note)
	{
		return IO::formatString("%d-%d", note.tick, note.lane);
	}

	static std::string noteKey(const Note& note)
	{
		return IO::formatString("%d-%d", note.tick, static_cast<int>(note.lane));
	}

	SerializingScore SusSerializer::susToScore(const SUS& sus)
	{
		ScoreMetadata metadata{
			sus.metadata.data.at("title"),
			sus.metadata.data.at("artist"),
			sus.metadata.data.at("designer"),
			"",
			"",
			sus.metadata.waveOffset * 1000 // seconds -> milliseconds
		};
		metadata.isExtendedScore = false;

		id_t nextNoteID = 0, nextHoldID = 0, nextHiSpeedID = 0;
		std::unordered_map<std::string, FlickType> flicks;
		std::unordered_set<std::string> criticals;
		std::unordered_set<std::string> stepIgnore;
		std::unordered_set<std::string> easeIns;
		std::unordered_set<std::string> easeOuts;
		std::unordered_set<std::string> slideKeys;
		std::unordered_set<std::string> frictions;
		std::unordered_set<std::string> hiddenHolds;

		// Taps on the same key as a slide point belong to the slide.
		// Guides can share a key with other notes
		for (const auto& slide : sus.slides)
		{
			for (const auto& note : slide)
			{
				switch (note.type)
				{
				case 1:
				case 2:
				case 3:
				case 5:
					slideKeys.insert(noteKey(note));
				}
			}
		}

		for (const auto& dir : sus.directionals)
		{
			const std::string key = noteKey(dir);
			switch (dir.type)
			{
			case 1:
				flicks.insert_or_assign(key, FlickType::Default);
				break;
			case 3:
				flicks.insert_or_assign(key, FlickType::Left);
				break;
			case 4:
				flicks.insert_or_assign(key, FlickType::Right);
				break;
			case 2:
				easeIns.insert(key);
				break;
			case 5:
			case 6:
				easeOuts.insert(key);
				break;
			default:
				break;
			}
		}

		for (const auto& tap : sus.taps)
		{
			const std::string key = noteKey(tap);
			switch (tap.type)
			{
			case 2:
				criticals.insert(key);
				break;
			case 3:
				stepIgnore.insert(key);
				break;
			case 5:
				frictions.insert(key);
				break;
			case 6:
				criticals.insert(key);
				frictions.insert(key);
				break;
			case 7:
				hiddenHolds.insert(key);
				break;
			case 8:
				hiddenHolds.insert(key);
				criticals.insert(key);
				break;
			default:
				break;
			}
		}

		NoteCollection notes;
		notes.reserve(sus.taps.size());

		HoldNoteCollection holds;
		holds.reserve(sus.slides.size());

		SkillTriggerCollection skills;
		Fever fever{ -1, -1 };

		for (const auto& note : sus.taps)
		{
			if (note.type == 4)
			{
				skills.insert(Skill{ note.tick });
			}
			else if (note.lane == 15 && note.width == 1)
			{
				if (note.type == 1)
					fever.startTick = note.tick;
				else if (note.type == 2)
					fever.endTick = note.tick;
			}
			else if (note.type == 7 || note.type == 8)
			{
				// Invisible slide tap point
				continue;
			}

			if (note.lane - 2 < MIN_LANE || note.lane - 2 > MAX_LANE)
				continue;

			const std::string key = noteKey(note);

			// Conflict with skip slide steps and hidden holds
			if (slideKeys.find(key) != slideKeys.end())
				continue;

			Note n = fromSUSNote(note, NoteType::Tap);
			n.flag = setFlag(n.flag, NoteFlag::Critical, criticals.find(key) != criticals.end());
			n.flag = setFlag(n.flag, NoteFlag::Trace, frictions.find(key) != frictions.end());
			n.flick = flicks.find(key) != flicks.end() ? flicks[key] : FlickType::None;
			n.ID = nextNoteID++;

			notes[n.ID] = n;
		}

		// Not the best way to handle this but it will do the job
		auto slideFillFunc = [&](const std::vector<std::vector<SUSNote>>& slides, bool isGuide)
		{
			for (const auto& slide : slides)
			{
				const std::string key = noteKey(slide[0]);

				auto start = std::find_if(slide.begin(), slide.end(), [](const SUSNote& a)
				                          { return a.type == 1 || a.type == 2; });

				if (start == slide.end() || slide.size() < 2)
					continue;

				bool critical = criticals.find(key) != criticals.end();

				HoldNote hold;
				hold.ID = nextHoldID++;
				hold.steps.reserve(slide.size());
				hold.fadeType = FadeType::Classic;

				HoldNoteStep& separator = hold.separators.emplace_back();
				if (isGuide)
				{
					separator.flag = setFlag(separator.flag, HoldNoteFlag::Guide);
					separator.guideColor = critical ? GuideColor::Yellow : GuideColor::Green;
					separator.layer = HoldStepLayer::Bottom;
				}
				else
				{
					separator.flag = setFlag(separator.flag, HoldNoteFlag::Critical, critical);
					separator.layer = HoldStepLayer::Top;
				}

				Note startNote, endNote;

				for (const auto& note : slide)
				{
					const std::string key = noteKey(note);

					EaseType ease = EaseType::Linear;
					if (easeIns.find(key) != easeIns.end())
					{
						ease = EaseType::EaseIn;
					}
					else if (easeOuts.find(key) != easeOuts.end())
					{
						ease = EaseType::EaseOut;
					}

					switch (note.type)
					{
						// start
					case 1:
					{
						if (startNote.ID >= 0)
							break;
						startNote = fromSUSNote(note, NoteType::Tap);
						startNote.flag = setFlag(startNote.flag, NoteFlag::Critical, critical);
						startNote.ID = nextNoteID++;
						startNote.holdID = hold.ID;

						if (isGuide)
						{
							startNote.flag = setFlag(startNote.flag, NoteFlag::Hidden);
						}
						else
						{
							bool isTrace = frictions.find(noteKey(note)) != frictions.end();
							bool isHidden = hiddenHolds.find(noteKey(note)) != hiddenHolds.end();
							startNote.flag = setFlag(startNote.flag, NoteFlag::Trace, isTrace);
							startNote.flag = setFlag(startNote.flag, NoteFlag::Hidden, isHidden);
						}
						startNote.ease = ease;

						notes[startNote.ID] = startNote;
					}
					break;
					// end
					case 2:
					{
						if (endNote.ID >= 0)
							break;
						endNote = fromSUSNote(note, NoteType::Tap);
						endNote.flag =
						    setFlag(endNote.flag, NoteFlag::Critical,
						            critical || (criticals.find(key) != criticals.end()));
						endNote.ID = nextNoteID++;
						endNote.holdID = hold.ID;

						if (isGuide)
						{
							endNote.flag = setFlag(endNote.flag, NoteFlag::Hidden);
						}
						else
						{
							bool isTrace = frictions.find(noteKey(note)) != frictions.end();
							bool isHidden = hiddenHolds.find(noteKey(note)) != hiddenHolds.end();
							endNote.flick =
							    flicks.find(key) != flicks.end() ? flicks[key] : FlickType::None;
							endNote.flag = setFlag(endNote.flag, NoteFlag::Trace, isTrace);
							endNote.flag = setFlag(endNote.flag, NoteFlag::Hidden, isHidden);
						}

						notes[endNote.ID] = endNote;
					}
					break;
					// mid
					case 3:
					case 5:
					{
						Note mid = fromSUSNote(note, NoteType::Tick);
						mid.flag = setFlag(mid.flag, NoteFlag::Critical, critical);
						mid.flag = setFlag(mid.flag, NoteFlag::Hidden, isGuide || note.type == 5);
						mid.flag = setFlag(mid.flag, NoteFlag::Attached, !isGuide && stepIgnore.count(key));
						mid.ID = nextNoteID++;
						mid.holdID = hold.ID;
						mid.ease = ease;

						notes[mid.ID] = mid;
						hold.steps.push_back(mid.ID);
					}
					break;
					default:
						break;
					}
				}

				hold.steps.insert(hold.steps.begin(), startNote.ID);
				hold.steps.push_back(endNote.ID);

				if (startNote.ID < 0 || endNote.ID < 0)
					throw std::runtime_error("Invalid hold note");

				hold.sortSteps(notes, true);
				holds[hold.ID] = std::move(hold);
			}
		};

		slideFillFunc(sus.slides, false);
		slideFillFunc(sus.guides, true);

		TempoCollection tempos;
		if (sus.bpms.size() == 0)
			tempos[0] = Tempo{ 0, 120 };
		for (const auto& tempo : sus.bpms)
			tempos[tempo.tick] = Tempo{ tempo.tick, tempo.bpm };

		TimeSignatureCollection timeSignatures;
		for (const auto& sign : sus.barlengths)
		{
			auto fraction = barLengthToFraction(sign.length, 4.0f);
			timeSignatures[sign.bar] = TimeSignature{ sign.bar, fraction.first, fraction.second };
		}

		HiSpeedCollection hiSpeedChanges;
		hiSpeedChanges.reserve(sus.hiSpeeds.size());
		for (const auto& speed : sus.hiSpeeds)
		{
			HiSpeed hispeed;
			hispeed.ID = nextHiSpeedID++;
			hispeed.tick = speed.tick;
			hispeed.speed = speed.speed;
			hiSpeedChanges[hispeed.ID] = hispeed;
		}

		Score score;
		score.notes = notes;
		score.holdNotes = holds;
		score.tempoChanges = tempos;
		score.timeSignatures = timeSignatures;
		score.hiSpeedChanges = hiSpeedChanges;
		score.skills = skills;
		score.fever = fever;

		return { score, metadata };
	}

	SUS SusSerializer::scoreToSus(const SerializingScore& data)
	{
		const Score& score = data.score;
		constexpr std::array<int, 4> flickToType{ 0, 1, 3, 4 };

		std::vector<SUSNote> taps, directionals;
		std::vector<std::vector<SUSNote>> slides;
		std::vector<std::vector<SUSNote>> guides;
		std::vector<SUSBPM> bpms;
		std::vector<BarLength> barlengths;
		std::vector<SUSHiSpeed> hiSpeeds;

		std::unordered_set<std::string> criticalKeys;
		for (const auto& [id, note] : score.notes)
		{
			if (note.isHold())
				continue;

			if (note.type == NoteType::Tap)
			{
				int type = note.isTrace() ? 5 : 1;
				if (note.isCrit())
				{
					type++;
					criticalKeys.insert(noteKey(note));
				}
				taps.push_back(toSUSNote(note, type));

				if (note.isFlick())
					directionals.push_back(
					    toSUSNote(note, flickToType[static_cast<int>(note.flick)]));
			}
		}

		/*
		Generally guides can be placed on the same tick and lane (key) as other notes.
		If one of those notes or the guide is critical however, it will cause all the notes with the
		same key to be critical.
		*/
		std::vector<HoldNote> exportHolds;
		exportHolds.reserve(score.holdNotes.size());

		for (const auto& [_, hold] : score.holdNotes)
			exportHolds.emplace_back(hold);

		std::sort(exportHolds.begin(), exportHolds.end(),
		          [&score](const HoldNote& a, const HoldNote& b)
		          {
			          const Note& n1 = score.notes.at(a.steps.front());
			          const Note& n2 = score.notes.at(b.steps.front());
			          return n1.tick == n2.tick ? a.ID < b.ID : n1.tick < n2.tick;
		          });

		for (const auto& hold : exportHolds)
		{
			std::vector<SUSNote> slide;
			slide.reserve(hold.steps.size() + 2);

			const Note& start = score.notes.at(hold.steps.front());

			slide.push_back(toSUSNote(start, 1));

			const HoldNoteStep& separator = hold.separators.front();
			bool isGuide = separator.isGuide();
			// Guide notes are always hidden, only a hidden hold has invisible start and end tap points
			bool startHidden = !isGuide && start.isHidden();
			bool startCritical =
					start.isCrit() || (isGuide && separator.guideColor == GuideColor::Yellow);
			bool hasEase = start.ease != EaseType::Linear;
			bool startHasCritKey = criticalKeys.find(noteKey(start)) != criticalKeys.end();
			bool invisibleTapPoint = startHidden || (isGuide && hasEase) || (isGuide && startCritical);

			if (hasEase)
				directionals.push_back(toSUSNote(start, start.ease == EaseType::EaseIn ? 2 : 6));

			bool guideAlreadyOnCritical = isGuide && startHasCritKey;

			// We'll use type 1 to indicate it's a normal note
			int type = invisibleTapPoint ? 7 : start.isTrace() ? 5 : 1;
			if (!startHasCritKey && startCritical)
			{
				type++;
				criticalKeys.insert(noteKey(start));
				startHasCritKey = true;
			}

			if (type > 1 && !((type == 7 || type == 8) && guideAlreadyOnCritical))
				taps.push_back(toSUSNote(start, type));

			for (size_t i = 1, n = hold.steps.size() - 1; i < n; i++)
			{
				const Note& midNote = score.notes.at(hold.steps[i]);

				slide.push_back(toSUSNote(midNote, midNote.isHidden() ? 5 : 3));
				if (midNote.isAttached())
				{
					taps.push_back(toSUSNote(midNote, 3));
				}
				else if (midNote.ease != EaseType::Linear)
				{
					int stepTapType = isGuide ? (startCritical ? 8 : 7) : 1;
					taps.push_back(toSUSNote(midNote, stepTapType));
					directionals.push_back(
					    toSUSNote(midNote, midNote.ease == EaseType::EaseIn ? 2 : 6));
				}
			}

			const Note& end = score.notes.at(hold.steps.back());

			slide.push_back(toSUSNote(end, 2));

			// Hidden and guide slides do not have flicks
			if (end.isFlick() && !end.isHidden() && !isGuide)
			{
				directionals.push_back(toSUSNote(end, flickToType[static_cast<int>(end.flick)]));
				// Critical friction notes use type 6 not 2
				if (end.isCrit() && !startCritical && !end.isTrace())
					taps.push_back(toSUSNote(end, 2));
			}

			bool endHidden = !isGuide && end.isHidden();
			bool endCritical = end.isCrit() || (isGuide && separator.guideColor == GuideColor::Yellow);
			int endType = endHidden ? 7 : end.isTrace() ? 5 : 1;
			if (endCritical)
			{
				endType++;
				criticalKeys.insert(noteKey(end));
			}

			if (endType != 1 && endType != 2)
				taps.push_back(toSUSNote(end, endType));

			if (isGuide)
			{
				guides.push_back(slide);
			}
			else
			{
				slides.push_back(slide);
			}
		}

		for (const auto& skill : score.skills)
			taps.push_back(SUSNote{ skill.tick, 0, 1, 4 });

		if (score.fever.startTick != -1)
			taps.push_back(SUSNote{ score.fever.startTick, 15, 1, 1 });

		if (score.fever.endTick != -1)
			taps.push_back(SUSNote{ score.fever.endTick, 15, 1, 2 });

		for (const auto& [_, tempo] : score.tempoChanges)
			bpms.push_back(SUSBPM{ tempo.tick, tempo.quarterPerMinute });

		if (!bpms.size())
			bpms.push_back(SUSBPM{ 0, 120 });

		for (const auto& [measure, ts] : score.timeSignatures)
			barlengths.push_back(
			    BarLength{ ts.measure, ((float)ts.numerator / (float)ts.denominator) * 4 });

		hiSpeeds.reserve(score.hiSpeedChanges.size());
		for (const auto& [_, hiSpeed] : score.hiSpeedChanges)
			hiSpeeds.push_back(SUSHiSpeed{ hiSpeed.tick, hiSpeed.speed });

		std::stable_sort(hiSpeeds.begin(), hiSpeeds.end(),
			[](const SUSHiSpeed& a, const SUSHiSpeed& b) { return a.tick < b.tick; });

		SUSMetadata metadata;
		metadata.data["title"] = data.metadata.title;
		metadata.data["artist"] = data.metadata.artist;
		metadata.data["designer"] = data.metadata.author;
		metadata.requests.push_back("ticks_per_beat 480");

		// milliseconds -> seconds
		metadata.waveOffset = data.metadata.musicOffset / 1000.0f;

		return SUS{ metadata, taps, directionals, slides, guides, bpms, barlengths, hiSpeeds };
	}
}
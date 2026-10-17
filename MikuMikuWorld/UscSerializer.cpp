#include "UscSerializer.h"
#include "IO.h"
#include "File.h"
#include "JsonIO.h"
#include "Localization.h"
#include "Math.h"
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace MikuMikuWorld
{
	static double toUscBeat(tick_t tick) { return tick / static_cast<double>(TICKS_PER_QUARTER); }

	static tick_t fromUscBeat(double beat)
	{
		return std::clamp<tick_t>(std::llround(beat * TICKS_PER_QUARTER), 0, MAX_TICK);
	}

	static float toUscLane(float lane, float width) { return lane - 6.0f + width / 2.0f; }

	static float fromUscLane(float lane, float size) { return lane + 6.0f - size; }

	static float sizeToWidth(float size) { return size * 2; }

	static float widthToSize(float width) { return width / 2; }

	static const char* toUscEase(EaseType ease)
	{
		if (ease >= EaseType::EaseNone)
			ease = EaseType::Linear;
		return easeTypes[static_cast<size_t>(ease)];
	}

	static EaseType fromUscEase(const json& obj)
	{
		std::string easeString = jsonIO::tryGetValue<std::string>(obj, "ease", "linear");
		std::transform(easeString.begin(), easeString.end(), easeString.begin(), ::tolower);
		return arrayFindOrDefault(easeTypes, easeString, EaseType::Linear);
	}

	static constexpr const char* uscFlickNames[]{ "none", "up", "left", "right" };
	// Down flicks convert to up flick, gameplay is unaffected
	static const char* toUscDirection(FlickType flick)
	{
		switch (flick)
		{
		case FlickType::None:
		case FlickType::Default:
		case FlickType::Left:
		case FlickType::Right:
			break;
		case FlickType::Down:
			flick = FlickType::Default;
			break;
		case FlickType::DownLeft:
			flick = FlickType::Left;
			break;
		case FlickType::DownRight:
			flick = FlickType::Right;
			break;
		default:
			flick = FlickType::None;
		}
		return uscFlickNames[int(flick)];
	}

	static FlickType fromUscDirection(const json& obj)
	{
		std::string dirString = jsonIO::tryGetValue<std::string>(obj, "direction", "none");
		std::transform(dirString.begin(), dirString.end(), dirString.begin(), ::tolower);
		return arrayFindOrDefault(uscFlickNames, dirString, FlickType::None);
	}

	static const char* toUscJudgeType(const Note& note)
	{
		return note.isHidden() ? "none" : note.isTrace() ? "trace" : "normal";
	}

	static NoteFlag fromUscJudgeType(const json& obj)
	{
		std::string judge = jsonIO::tryGetValue<std::string>(obj, "judgeType", "normal");
		if (judge == "trace")
			return NoteFlag::Trace;
		if (judge == "none")
			return NoteFlag::Hidden;
		return NoteFlag::None;
	}

	static const char* toUscColor(GuideColor color) { return guideColors[static_cast<int>(color)]; }

	static GuideColor fromUscColor(const json& obj)
	{
		std::string colorString = jsonIO::tryGetValue<std::string>(obj, "color", "green");
		std::transform(colorString.begin(), colorString.end(), colorString.begin(), ::tolower);
		return arrayFindOrDefault(guideColors, colorString, GuideColor::Green);
	}

	// Custom and classic fades are handled before this is called
	static const char* toUscFade(FadeType fade)
	{
		return fade == FadeType::None ? "none" : fade == FadeType::In ? "in" : "out";
	}

	static FadeType fromUscFade(const json& obj)
	{
		std::string fade = jsonIO::tryGetValue<std::string>(obj, "fade", "out");
		return fade == "none" ? FadeType::None : fade == "in" ? FadeType::In : FadeType::Out;
	}

	static json toUscBaseNote(const Note& note)
	{
		json obj;
		obj["beat"] = toUscBeat(note.tick);
		obj["size"] = widthToSize(note.width);
		obj["lane"] = toUscLane(note.lane, note.width);
		obj["timeScaleGroup"] = note.layer;
		return obj;
	}

	static Note fromUscBaseNote(const json& obj, NoteType type, int defaultLayer = 0)
	{
		Note note;
		note.type = type;
		note.tick = fromUscBeat(obj.at("beat").get<double>());
		note.layer = jsonIO::tryGetValue<int>(obj, "timeScaleGroup", defaultLayer);
		if ((obj.contains("lane") && obj.contains("size")))
		{
			float size = obj.at("size").get<float>(), lane = obj.at("lane").get<float>();
			note.width = sizeToWidth(size);
			note.lane = fromUscLane(lane, size);
		}
		return note;
	}

	static auto getTimeScaleGroups(const Score& score)
	{
		std::vector<std::vector<HiSpeed>> groups(score.layers.size());

		for (const auto& [_, hiSpeed] : score.hiSpeedChanges)
			if (isArrayIndexInBounds(hiSpeed.layer, groups))
				groups[hiSpeed.layer].push_back(hiSpeed);

		for (auto& group : groups)
		{
			std::stable_sort(group.begin(), group.end(),
			                 [](const HiSpeed& a, const HiSpeed& b) { return a.tick < b.tick; });

			for (size_t i = 0; i < group.size(); ++i)
			{
				switch (group[i].ease)
				{
				case HiSpeedEaseType::None:
					continue;
				case HiSpeedEaseType::Linear:
				{
					// USC don't support Linear ease
					// We need to interpolate it
					constexpr tick_t tickPerStep = TICKS_PER_QUARTER / 8;
					if (i + 1 >= group.size())
						continue;
					HiSpeed curr = group[i];
					HiSpeed next = group[i + 1];
					tick_t tick = (curr.tick + tickPerStep) / tickPerStep * tickPerStep;
					tick_t diff = next.tick - tick;
					size_t count = (diff + tickPerStep - 1) / tickPerStep;
					group.insert(group.begin() + i + 1, count, {});
					for (size_t j = 1; tick < next.tick; tick += tickPerStep, j++)
					{
						float ratio = unlerp(curr.tick, next.tick, tick);
						float speed = lerp(curr.speed, next.speed, ratio);
						group[i + j].tick = tick;
						group[i + j].speed = speed;
					}
					break;
				}
				default:
					throw std::runtime_error("HiSpeedEaseType not supported");
				}
				group[i].ease = HiSpeedEaseType::None;
			}
		}

		return groups;
	}

	static auto getSingleNotes(const Score& score)
	{
		std::vector<const Note*> singles;
		for (const auto& [_, note] : score.notes)
		{
			if (note.isHold() || note.isHidden() ||
			    (note.type != NoteType::Tap && note.type != NoteType::Damage))
				continue;
			singles.push_back(&note);
		}
		auto noteCompare = [](const Note* a, const Note* b)
		{
			if (a->tick != b->tick)
				return a->tick < b->tick;
			if (a->lane != b->lane)
				return a->lane < b->lane;
			return a->ID < b->ID;
		};
		std::sort(singles.begin(), singles.end(), noteCompare);
		return singles;
	}

	struct LegacyHoldNote
	{
		Note start;
		Note end;
		std::vector<Note> steps;
		std::vector<Note> extra;

		HoldNoteFlag flag{ HoldNoteFlag::Normal };
		FadeType fadeType{ FadeType::Out };
		GuideColor guideColor{ GuideColor::Green };

		inline bool isGuide() const { return hasFlag(flag, HoldNoteFlag::Guide); }
	};

	static auto getLegacyHoldNotes(const Score& score)
	{
		auto setHidden = [](Note& note, NoteType type)
		{
			note.type = type;
			note.flag = setFlag(
			    note.flag, NoteFlag::Trace | NoteFlag::NonAttached | NoteFlag::Attached, false);
			note.flag = setFlag(note.flag, NoteFlag::Hidden);
			note.flick = FlickType::None;
		};

		std::vector<LegacyHoldNote> holds;
		for (const auto& [_, hold] : score.holdNotes)
		{
			LegacyHoldNote oldHold;
			oldHold.start = score.notes.at(hold.steps.front());
			oldHold.end = score.notes.at(hold.steps.back());

			oldHold.flag = hold.separators.front().flag;
			oldHold.guideColor = hold.separators.front().guideColor;
			oldHold.fadeType = hold.fadeType;

			if (oldHold.start.type != NoteType::Tap)
			{
				if (oldHold.start.type != NoteType::Tick)
					oldHold.extra.push_back(oldHold.start);
				else if (!oldHold.start.isHidden())
					oldHold.steps.insert(oldHold.steps.begin(), oldHold.start);
				setHidden(oldHold.start, NoteType::Tap);
			}
			else if ((oldHold.isGuide() && !oldHold.start.isHidden()) || oldHold.start.isFlick())
			{
				oldHold.extra.push_back(oldHold.start);
				setHidden(oldHold.start, NoteType::Tap);
			}
			for (size_t istep = 1, ijoint = 0; istep < hold.steps.size(); istep++)
			{
				const Note& endJoint = score.notes.at(hold.steps[istep]);
				if (endJoint.isAttached())
					continue;
				const Note& startJoint = score.notes.at(hold.steps[ijoint]);

				// Push notes between the joints
				for (size_t i = ijoint + 1; i < istep; i++)
				{
					Note step = score.notes.at(hold.steps[i]);
					if (step.type != NoteType::Tick || oldHold.isGuide())
					{
						float ratio = unlerp(startJoint.tick, endJoint.tick, step.tick);
						auto easeFunc = getEaseFunction(startJoint.ease);
						step.lane = easeFunc(startJoint.lane, endJoint.lane, ratio);
						step.width = easeFunc(startJoint.lane + startJoint.width,
						                      endJoint.lane + endJoint.width, ratio) -
						             step.lane;
						oldHold.extra.push_back(step);
					}
					else
					{
						oldHold.steps.push_back(step);
					}
				}
				// Fix easeNone, USC doesn't support it
				if (startJoint.ease == EaseType::EaseNone)
				{
					Note step;
					step.tick =
					    startJoint.tick >= endJoint.tick - 1 ? endJoint.tick : endJoint.tick - 1;
					step.lane = startJoint.lane;
					step.width = startJoint.width;
					step.layer = startJoint.layer;
					step.flag = setFlag(step.flag, NoteFlag::Hidden);
					oldHold.steps.push_back(step);
				}
				// Push back the current step, if it's not the end step
				if (istep < hold.steps.size() - 1)
				{
					Note step = endJoint;
					if (step.type != NoteType::Tick || (oldHold.isGuide() && !step.isHidden()))
					{
						oldHold.extra.push_back(step);
						setHidden(step, NoteType::Tick);
					}
					oldHold.steps.push_back(step);
				}
				ijoint = istep;
			}
			if (oldHold.end.type != NoteType::Tap)
			{
				if (oldHold.end.type != NoteType::Tick)
					oldHold.extra.push_back(oldHold.end);
				else if (!oldHold.end.isHidden())
					oldHold.steps.insert(oldHold.steps.end(), oldHold.end);
				setHidden(oldHold.end, NoteType::Tap);
			}
			else if (oldHold.isGuide() && !oldHold.end.isHidden())
			{
				oldHold.extra.push_back(oldHold.end);
				setHidden(oldHold.end, NoteType::Tap);
			}

			for (size_t i = 0; i < oldHold.extra.size();)
			{
				Note& extra = oldHold.extra[i];
				if (extra.isHidden())
				{
					oldHold.extra.erase(oldHold.extra.begin() + i);
				}
				else
				{
					if (extra.type == NoteType::Tick)
					{
						extra.type = NoteType::Tap;
						extra.flag = setFlag(extra.flag, NoteFlag::Trace);
						extra.flick = FlickType::None;
					}
					i++;
				}
			}
			holds.push_back(std::move(oldHold));
		}

		std::stable_sort(holds.begin(), holds.end(),
		                 [](const LegacyHoldNote& a, const LegacyHoldNote& b)
		                 {
			                 return a.start.tick != b.start.tick ? a.start.tick < b.start.tick
			                                                     : a.start.ID < b.start.ID;
		                 });
		return holds;
	}

	static bool isValidUscScore(const Score& score)
	{
		for (const auto& [_, hiSpeed] : score.hiSpeedChanges)
		{
			if (hiSpeed.skips != 0.0f || hiSpeed.hideNotes)
				return false;
			if (hiSpeed.ease != HiSpeedEaseType::None && hiSpeed.ease != HiSpeedEaseType::Linear)
				return false;
		}

		return true;
	}

	// Remove notes that reference a layer that does not exist. A hold with an invalid step is
	// removed as a whole
	static void removeInvalidNotes(Score& score)
	{
		const id_t layerCount = static_cast<id_t>(score.layers.size());
		auto isValidLayer = [layerCount](id_t layer) { return layer >= 0 && layer < layerCount; };

		for (auto it = score.holdNotes.begin(); it != score.holdNotes.end();)
		{
			const HoldNote& hold = it->second;
			bool valid = hold.steps.size() >= 2;
			for (id_t id : hold.steps)
			{
				auto note = score.notes.find(id);
				valid = valid && note != score.notes.end() && isValidLayer(note->second.layer);
			}

			if (valid)
			{
				++it;
				continue;
			}
			for (id_t id : hold.steps)
				score.notes.erase(id);
			it = score.holdNotes.erase(it);
		}

		for (auto it = score.notes.begin(); it != score.notes.end();)
			it = isValidLayer(it->second.layer) ? std::next(it) : score.notes.erase(it);
	}

	void UscSerializer::serialize(const SerializingScore& data, std::string filename)
	{
		json usc = scoreToUsc(data);
		IO::File uscfile(filename, IO::FileMode::Write);
		uscfile.write(usc.dump(minify ? -1 : 4));
		uscfile.flush();
		uscfile.close();
	}

	SerializingScore UscSerializer::deserialize(std::string filename)
	{
		IO::File uscFile(filename, IO::FileMode::Read);
		json usc = json::parse(uscFile.readAllText());
		uscFile.close();
		return uscToScore(usc);
	}

	json UscSerializer::scoreToUsc(const SerializingScore& data)
	{
		const Score& score = data.score;
		const ScoreMetadata& metadata = data.metadata;
		json vusc;
		json usc;

		usc["offset"] = metadata.musicOffset / -1000.0f;

		std::vector<json> objects;

		for (const auto& [_, tempo] : score.tempoChanges)
		{
			json& obj = objects.emplace_back();
			obj["type"] = "bpm";
			obj["beat"] = toUscBeat(tempo.tick);
			obj["bpm"] = tempo.quarterPerMinute;
		}

		for (const auto& group : getTimeScaleGroups(score))
		{
			json& timeScaleGroup = objects.emplace_back();
			timeScaleGroup["type"] = "timeScaleGroup";
			timeScaleGroup["changes"] = json::array();

			for (const auto& hs : group)
			{
				if (hs.skips != 0.0f || hs.hideNotes)
					throw std::runtime_error("USC don't support this HiSpeed type");
				json& obj = timeScaleGroup["changes"].emplace_back();
				obj["beat"] = toUscBeat(hs.tick);
				obj["timeScale"] = hs.speed;
			}
		}

		for (const Note* note : getSingleNotes(score))
		{
			json& obj = objects.emplace_back(toUscBaseNote(*note));
			if (note->type == NoteType::Tap)
			{
				obj["type"] = "single";
				obj["critical"] = note->isCrit();
				obj["trace"] = note->isTrace();
				if (note->isFlick())
				{
					obj["direction"] = toUscDirection(note->flick);
				}
			}
			else if (note->type == NoteType::Damage)
			{
				obj["type"] = "damage";
			}
		}

		for (const auto& note : getLegacyHoldNotes(score))
		{
			json& obj = objects.emplace_back();
			if (note.isGuide())
			{
				obj["type"] = "guide";
				obj["color"] = guideColors[(int)note.guideColor];
				obj["fade"] = toUscFade(note.fadeType);

				json& midpoints = obj["midpoints"] = json::array();

				json& startStep = midpoints.emplace_back(toUscBaseNote(note.start));
				startStep["ease"] = toUscEase(note.start.ease);

				for (const auto& step : note.steps)
				{
					json& stepObj = midpoints.emplace_back(toUscBaseNote(step));
					stepObj["ease"] = toUscEase(step.ease);
				}

				json& endStep = midpoints.emplace_back(toUscBaseNote(note.end));
				endStep["ease"] = "linear";

				for (const Note& extra : note.extra)
				{
					if (extra.type != NoteType::Tap && extra.type != NoteType::Damage)
						continue;
					json& obj = objects.emplace_back(toUscBaseNote(extra));
					if (extra.type == NoteType::Tap)
					{
						obj["type"] = "single";
						obj["critical"] = extra.isCrit();
						obj["trace"] = extra.isTrace();
						if (extra.isFlick())
						{
							obj["direction"] = toUscDirection(extra.flick);
						}
					}
					else if (extra.type == NoteType::Damage)
					{
						obj["type"] = "damage";
					}
				}
				continue;
			}
			obj["type"] = "slide";
			obj["critical"] = note.start.isCrit();

			json& connections = obj["connections"] = json::array();

			json& startStep = connections.emplace_back(toUscBaseNote(note.start));
			startStep["type"] = "start";
			startStep["critical"] = note.start.isCrit();
			startStep["ease"] = toUscEase(note.start.ease);
			startStep["judgeType"] = toUscJudgeType(note.start);

			for (const auto& step : note.steps)
			{
				json& stepObj = connections.emplace_back(toUscBaseNote(step));
				stepObj["type"] = step.isAttached() ? "attach" : "tick";
				if (!step.isHidden())
					stepObj["critical"] = step.isCrit();
				stepObj["ease"] = toUscEase(step.ease);
			}

			json& endStep = connections.emplace_back(toUscBaseNote(note.end));
			endStep["type"] = "end";
			endStep["critical"] = note.end.isCrit();
			endStep["judgeType"] = toUscJudgeType(note.end);
			if (note.end.isFlick())
				endStep["direction"] = toUscDirection(note.end.flick);

			for (const Note& extra : note.extra)
			{
				if (extra.type != NoteType::Tap && extra.type != NoteType::Damage)
					continue;
				json& obj = objects.emplace_back(toUscBaseNote(extra));
				if (extra.type == NoteType::Tap)
				{
					obj["type"] = "single";
					obj["critical"] = extra.isCrit();
					obj["trace"] = extra.isTrace();
					if (extra.isFlick())
					{
						obj["direction"] = toUscDirection(extra.flick);
					}
				}
				else if (extra.type == NoteType::Damage)
				{
					obj["type"] = "damage";
				}
			}
		}

		usc["objects"] = std::move(objects);

		vusc["version"] = 2;
		vusc["usc"] = std::move(usc);
		return vusc;
	}

	SerializingScore UscSerializer::uscToScore(const json& vusc)
	{
		Score score;
		ScoreMetadata metadata;
		if (vusc["version"] != 2)
		{
			throw std::runtime_error("Invalid version");
		}
		json usc = vusc.at("usc");
		score.layers.clear();
		score.hiSpeedChanges.clear();
		score.tempoChanges.clear();

		metadata.isExtendedScore = true;
		metadata.musicOffset = usc["offset"].get<float>() * -1000.0f;

		id_t nextNoteID = 0, nextHoldID = 0, nextHiSpeedID = 0;
		for (const auto& obj : usc["objects"].get<std::vector<json>>())
		{
			if (obj["type"] == "bpm")
			{
				Tempo tempo;
				tempo.tick = fromUscBeat(obj["beat"].get<double>());
				tempo.quarterPerMinute = obj["bpm"].get<float>();
				score.tempoChanges[tempo.tick] = tempo;
			}
			else if (obj["type"] == "timeScaleGroup")
			{
				int index = score.layers.size();
				score.layers.push_back(Layer{ IO::formatString("#%d", index) });
				std::vector<HiSpeed> hiSpeeds;
				for (const auto& change : obj["changes"])
				{
					HiSpeed hiSpeed;
					hiSpeed.ID = nextHiSpeedID++;
					hiSpeed.tick = fromUscBeat(change["beat"].get<double>());
					hiSpeed.layer = index;
					hiSpeed.speed = change.at("timeScale").get<float>();

					hiSpeeds.push_back(hiSpeed);
				}
				std::stable_sort(hiSpeeds.begin(), hiSpeeds.end(),
				                 [](const HiSpeed& a, const HiSpeed& b)
				                 { return a.tick < b.tick; });
				// Hi-speeds of the same layer cannot share a tick
				// push duplicates to the next free tick
				for (size_t i = 1; i < hiSpeeds.size(); ++i)
					if (hiSpeeds[i].tick <= hiSpeeds[i - 1].tick)
						hiSpeeds[i].tick = hiSpeeds[i - 1].tick + 1;
				for (const HiSpeed& hiSpeed : hiSpeeds)
					score.hiSpeedChanges[hiSpeed.ID] = hiSpeed;
			}
			else if (obj["type"] == "single")
			{
				Note note = fromUscBaseNote(obj, NoteType::Tap);
				note.flag = setFlag(note.flag, NoteFlag::Critical, obj["critical"].get<bool>());
				note.flag = setFlag(note.flag, NoteFlag::Trace, obj["trace"].get<bool>());
				if (obj.contains("direction"))
				{
					note.flick = fromUscDirection(obj);
				}
				else
				{
					note.flick = FlickType::None;
				}
				note.ID = nextNoteID++;
				score.notes[note.ID] = note;
			}
			else if (obj["type"] == "damage")
			{
				Note note = fromUscBaseNote(obj, NoteType::Damage);
				note.ID = nextNoteID++;
				score.notes[note.ID] = note;
			}
			else if (obj["type"] == "guide")
			{
				const json& midpoints = obj.at("midpoints");
				if (midpoints.size() < 2)
					continue;

				HoldNote hold;
				hold.ID = nextHoldID++;

				HoldNoteStep& separator = hold.separators.emplace_back();
				separator.flag = HoldNoteFlag::Guide;
				separator.layer = HoldStepLayer::Top;
				separator.guideColor = fromUscColor(obj);
				hold.fadeType = fromUscFade(obj);

				for (size_t i = 0; i < midpoints.size(); i++)
				{
					const json& midpoint = midpoints[i];
					bool isLast = i == midpoints.size() - 1;
					NoteType type = (i == 0 || isLast) ? NoteType::Tap : NoteType::Tick;
					Note note = fromUscBaseNote(midpoint, type);
					note.ID = nextNoteID++;
					note.holdID = hold.ID;
					note.ease = isLast ? EaseType::Linear : fromUscEase(midpoint);
					note.flag = setFlag(note.flag, NoteFlag::Hidden);
					hold.steps.push_back(note.ID);
					score.notes[note.ID] = note;
				}
				separator.ID = hold.steps.front();
				hold.sortSteps(score.notes);
				score.holdNotes[hold.ID] = hold;
			}
			else if (obj["type"] == "slide")
			{
				HoldNote hold;
				hold.fadeType = FadeType::None;

				auto connections = obj["connections"].get<std::vector<json>>();
				std::stable_sort(
				    connections.begin(), connections.end(),
				    [](const json& a, const json& b)
				    {
					    int ordA = a["type"] == "start" ? 0 : a["type"] == "end" ? 2 : 1;
					    int ordB = b["type"] == "start" ? 0 : b["type"] == "end" ? 2 : 1;
					    if (ordA != ordB)
						    return ordA < ordB;
					    return a["beat"].get<double>() < b["beat"].get<float>();
				    });

				if (connections.size() < 2 || connections.front()["type"] != "start" ||
				    connections.back()["type"] != "end")
					continue;
				hold.ID = nextHoldID++;
				bool isCritical = false;
				int startLayer = 0;
				// Attach notes only have a time, their lane and width are estimated afterwards
				std::unordered_set<id_t> attachedIDs;
				for (const auto& step : connections)
				{
					auto type = step["type"].get<std::string>();
					if (type == "start")
					{
						Note startNote = fromUscBaseNote(step, NoteType::Tap);
						isCritical = step["critical"].get<bool>();
						startLayer = startNote.layer;
						startNote.flag = setFlag(startNote.flag, NoteFlag::Critical, isCritical) |
						                 fromUscJudgeType(step);
						startNote.ease = fromUscEase(step);

						startNote.ID = nextNoteID++;
						startNote.holdID = hold.ID;
						score.notes[startNote.ID] = startNote;
						hold.steps.push_back(startNote.ID);
					}
					else if (type == "end")
					{
						Note endNote = fromUscBaseNote(step, NoteType::Tap);
						endNote.flick = fromUscDirection(step);
						endNote.flag |= fromUscJudgeType(step);

						bool endCritical = jsonIO::tryGetValue<bool>(step, "critical", false);
						bool endSpecial = endNote.flick != FlickType::None ||
						                  hasFlag(endNote.flag, NoteFlag::Trace);
						endNote.flag = setFlag(endNote.flag, NoteFlag::Critical,
						                       isCritical || (endCritical && endSpecial));

						endNote.ID = nextNoteID++;
						endNote.holdID = hold.ID;
						score.notes[endNote.ID] = endNote;
						hold.steps.push_back(endNote.ID);
					}
					else
					{
						Note mid = fromUscBaseNote(step, NoteType::Tick, startLayer);
						mid.flag = setFlag(mid.flag, NoteFlag::Critical, isCritical);
						mid.ease = fromUscEase(step);
						mid.flag = setFlag(mid.flag, NoteFlag::Attached, type == "attach");
						mid.flag = setFlag(mid.flag, NoteFlag::Hidden,
						                   type == "tick" && !step.contains("critical"));

						mid.ID = nextNoteID++;
						mid.holdID = hold.ID;
						if (type == "attach" && !step.contains("lane"))
							attachedIDs.insert(mid.ID);
						score.notes[mid.ID] = mid;
						hold.steps.push_back(mid.ID);
					}
				}

				for (size_t i = 1; i + 1 < hold.steps.size(); i++)
				{
					Note& attach = score.notes.at(hold.steps[i]);
					if (!attachedIDs.count(attach.ID))
						continue;

					size_t head = i, tail = i;
					while (head > 0 &&
					       hasFlag(score.notes.at(hold.steps[head]).flag, NoteFlag::Attached))
						head--;
					while (tail + 1 < hold.steps.size() &&
					       hasFlag(score.notes.at(hold.steps[tail]).flag, NoteFlag::Attached))
						tail++;

					const Note& startJoint = score.notes.at(hold.steps[head]);
					const Note& endJoint = score.notes.at(hold.steps[tail]);
					float ratio = unlerp(startJoint.tick, endJoint.tick, attach.tick, 0.5f);
					auto easeFunc = getEaseFunction(startJoint.ease);
					attach.lane = easeFunc(startJoint.lane, endJoint.lane, ratio);
					attach.width = easeFunc(startJoint.lane + startJoint.width,
					                        endJoint.lane + endJoint.width, ratio) -
					               attach.lane;
				}

				HoldNoteStep& separator = hold.separators.emplace_back();
				separator.ID = hold.steps.front();
				separator.flag = isCritical ? HoldNoteFlag::Critical : HoldNoteFlag::Normal;
				separator.guideColor = isCritical ? GuideColor::Yellow : GuideColor::Green;
				separator.layer = HoldStepLayer::Top;
				hold.sortSteps(score.notes);

				for (auto&& ID : attachedIDs)
				{
					Note& attach = score.notes.at(ID);
					const Note* startJoint = hold.jointBeforeStep(attach, score.notes);
					const Note* endJoint = hold.jointAfterStep(attach, score.notes);
					if (!startJoint || !endJoint)
						continue;
					float ratio = unlerp(startJoint->tick, endJoint->tick, attach.tick, 0.5f);
					auto easeFunc = getEaseFunction(startJoint->ease);
					attach.lane = easeFunc(startJoint->lane, endJoint->lane, ratio);
					float rightLane = easeFunc(startJoint->lane + startJoint->width,
					                           endJoint->lane + endJoint->width, ratio);
					attach.width = rightLane - attach.lane;
				}

				score.holdNotes[hold.ID] = hold;
			}
		}

		if (score.layers.size() == 0)
		{
			score.layers.push_back(Layer{ "#0" });
		}
		if (score.tempoChanges.size() == 0)
		{
			score.tempoChanges[0] = Tempo{ 0, 120 };
		}
		if (score.hiSpeedChanges.size() == 0)
		{
			HiSpeed hiSpeed;
			hiSpeed.ID = nextHiSpeedID++;
			score.hiSpeedChanges[hiSpeed.ID] = hiSpeed;
		}

		removeInvalidNotes(score);

		// Recalculate lane extension from the notes
		for (const auto& [_, note] : score.notes)
		{
			int left = std::ceil(std::abs(note.lane - 6.0f));
			int right = std::ceil(std::abs(note.lane + note.width - 6.0f));
			metadata.laneExtension = std::max({ left - 6, right - 6, metadata.laneExtension });
		}

		return { score, metadata };
	}

	Result UscSerializer::canSerialize(const SerializingScore& data)
	{
		if (isValidUscScore(data.score))
			return Result::Ok();
		return Result(ResultStatus::Error, localize(Text::exportFormatNotAvailExtend).string);
	}
}
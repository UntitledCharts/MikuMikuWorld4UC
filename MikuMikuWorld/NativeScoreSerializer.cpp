#include "NativeScoreSerializer.h"
#include "Localization.h"

namespace MikuMikuWorld
{
	// Version 1: Version + Metadata(title, author, artist, musicFile, musicOffset) + TimeSignature
	// + TempoChange +	Note + Hold
	// Version 2: Skill Support, Jacket Support
	// Version 3: Hispeed Support, Store address offset (metadata, events, taps, holds)
	// Version 4: Guide Support, use platform path for metadata
	const int MMWS_VERSION = 4;
	const char* MMWS_SIGNATURE = "MMWS";
	// Version 1: Damage note support, Lane extension support
	// Version 2: Support hold fade type, Change version to int16
	// Version 3: Support more guide colors
	// Version 4: Support layers
	// Version 5: Support waypoints
	// Version 6: Use floating type for lane and width and ease inOut
	const int CC_MMWS_VERSION = 6;
	const char* CC_MMWS_SIGNATURE = "CCMMWS";
	// Version 1: Revert version to int32, support dummy note, dummy hold
	// Version 2: Support hispeed easing, skip, hides note; down flicks
	// Version 3: Note data structure refactor; skill effects; hispeed hidenotes; sound effect
	// Version 4: Force note speed
	const int UC_MMWS_VERSION = 4;
	const char* UC_MMWS_SIGNATURE = "UCMMWS";

	enum LegacyNoteType
	{
		Tap,
		Hold,
		HoldMid,
		HoldEnd,
		Damage,
	};
	enum LegacyNoteFlags
	{
		NOTE_CRITICAL = 1 << 0,
		NOTE_FRICTION = 1 << 1,
		NOTE_DUMMY = 1 << 2,
	};
	static_assert(static_cast<int>(NoteFlag::Critical) == NOTE_CRITICAL);
	static_assert(static_cast<int>(NoteFlag::Trace) == NOTE_FRICTION);
	static_assert(static_cast<int>(NoteFlag::Dummy) == NOTE_DUMMY);

	enum LegacyHoldFlags
	{
		HOLD_START_HIDDEN = 1 << 0,
		HOLD_END_HIDDEN = 1 << 1,
		HOLD_GUIDE = 1 << 2,
		HOLD_DUMMY = 1 << 3
	};

	struct NativeScoreSerializer::ScoreVersion
	{
		int version;
		int cyanvasVersion;
		int untitledVersion;

		ScoreVersion() : version(0), cyanvasVersion(0), untitledVersion(0) {}
		ScoreVersion(int ucVersion, int ccVersion = CC_MMWS_VERSION, int version = MMWS_VERSION)
		    : version(version), cyanvasVersion(ccVersion), untitledVersion(ucVersion)
		{
		}

		inline bool supportSkillFever() const { return version >= 2; }
		inline bool supportJacket() const { return version >= 2; }
		inline bool supportAddress() const { return version >= 3; }
		inline bool supportHispeed() const { return version >= 3; }
		inline bool supportGuideNote() const { return version >= 4; }
		inline bool supportDamageNote() const { return cyanvasVersion >= 1; }
		inline bool supportLaneExtension() const { return cyanvasVersion >= 1; }
		inline bool supportFadeType() const { return cyanvasVersion >= 2; }
		inline bool supportGuideColor() const { return cyanvasVersion >= 3; }
		inline bool supportLayers() const { return cyanvasVersion >= 4; }
		inline bool supportWaypoints() const { return cyanvasVersion >= 5; }
		inline bool supportFloatingLaneWidth() const { return cyanvasVersion >= 6; }
		inline bool supportDummyNote() const { return cyanvasVersion >= 6 && untitledVersion >= 1; }
		inline bool supportHispeedSkipEase() const { return untitledVersion >= 2; }
		inline bool supportDownFlick() const { return untitledVersion >= 2; }
		inline bool supportExtendedNote() const { return untitledVersion >= 3; }
		inline bool supportExtendedSkill() const { return untitledVersion >= 3; }
		inline bool supportSoundEffect() const { return untitledVersion >= 3; }
		inline bool supportLifePoint() const { return untitledVersion >= 3; }
		inline bool supportHoldLayer() const { return untitledVersion >= 3; }
		inline bool supportForceNoteSpeed() const { return untitledVersion >= 4; }

		inline bool isImplicitExtended() const { return cyanvasVersion > 0 || untitledVersion > 0; }
		inline HoldStepLayer getImplicitLayer(const HoldNoteStep& step) const
		{
			return isImplicitExtended() ? HoldStepLayer::Top
			       : step.isGuide()     ? HoldStepLayer::Bottom
			                            : HoldStepLayer::Top;
		}
		inline bool isSupportedVersion() const
		{
			return version <= MMWS_VERSION && cyanvasVersion <= CC_MMWS_VERSION &&
			       untitledVersion <= UC_MMWS_VERSION;
		}
	};

	Note NativeScoreSerializer::readNote(LegacyNoteType type, IO::BinaryReader& reader,
	                                     const ScoreVersion& version)
	{
		Note note;

		if (version.supportFloatingLaneWidth())
		{
			note.tick = reader.readUInt32();
			note.lane = reader.readSingle();
			note.width = reader.readSingle();
		}
		else
		{
			note.tick = reader.readUInt32();
			note.lane = reader.readUInt32();
			note.width = reader.readUInt32();
		}
		note.tick = std::max(note.tick, 0);

		if (version.supportSoundEffect())
			note.soundEffect = static_cast<SoundEffectType>(reader.readUInt32());

		if (version.supportLayers())
			note.layer = reader.readUInt32();

		if (version.supportExtendedNote())
		{
			note.type = static_cast<NoteType>(reader.readUInt32());
			note.flag = static_cast<NoteFlag>(reader.readUInt32());
			note.flick = static_cast<FlickType>(reader.readUInt32());
			note.ease = static_cast<EaseType>(reader.readUInt32());

			note.guideAlpha = reader.readSingle();
		}
		else
		{
			if (type != LegacyNoteType::Hold && type != LegacyNoteType::HoldMid)
				note.flick = static_cast<FlickType>(reader.readUInt32());
			constexpr uint32_t noteFlagDataMask = 0x1F; // prevent accidental set bit
			note.flag = static_cast<NoteFlag>(reader.readUInt32() & noteFlagDataMask);
			switch (type)
			{
			case LegacyNoteType::Tap:
			case LegacyNoteType::Hold:
			case LegacyNoteType::HoldEnd:
				note.type = NoteType::Tap;
				break;
			case LegacyNoteType::HoldMid:
				note.type = NoteType::Tick;
				break;
			case LegacyNoteType::Damage:
				note.type = NoteType::Damage;
				break;
			}
		}
		if (note.ease >= EaseType::EaseTypeCount)
			note.ease = EaseType::Linear;
		if (note.flick >= FlickType::FlickTypeCount ||
		    (note.flick >= FlickType::Down && !version.supportDownFlick()))
			note.flick = FlickType::Default;
		if (!version.supportDummyNote())
			note.flag = setFlag(note.flag, NoteFlag::Dummy, false);

		return note;
	}

	void NativeScoreSerializer::writeNote(const Note& note, IO::BinaryWriter& writer)
	{
		writer.writeInt32(note.tick);
		writer.writeSingle(note.lane);
		writer.writeSingle(note.width);
		writer.writeInt32(static_cast<uint32_t>(note.soundEffect));

		writer.writeInt32(note.layer);

		writer.writeInt32(static_cast<uint32_t>(note.type));
		writer.writeInt32(static_cast<uint32_t>(note.flag));
		writer.writeInt32(static_cast<uint32_t>(note.flick));
		writer.writeInt32(static_cast<uint32_t>(note.ease));

		writer.writeSingle(note.guideAlpha);
	}

	ScoreMetadata NativeScoreSerializer::readMetadata(IO::BinaryReader& reader,
	                                                  const ScoreVersion& version)
	{
		ScoreMetadata metadata;
		metadata.title = reader.readString();
		metadata.author = reader.readString();
		metadata.artist = reader.readString();
		metadata.musicFile = reader.readString();
		metadata.musicOffset = reader.readSingle();

		if (version.supportJacket())
			metadata.jacketFile = reader.readString();

		if (version.supportLaneExtension())
			metadata.laneExtension = reader.readUInt32();

		if (version.supportExtendedNote())
			metadata.isExtendedScore = reader.readUInt32();
		else
			metadata.isExtendedScore = version.isImplicitExtended();

		if (version.supportLifePoint())
			metadata.baseLifePoint = reader.readUInt32();

		return metadata;
	}

	void NativeScoreSerializer::writeMetadata(const ScoreMetadata& metadata,
	                                          IO::BinaryWriter& writer)
	{
		writer.writeString(metadata.title);
		writer.writeString(metadata.author);
		writer.writeString(metadata.artist);
		writer.writeString(metadata.musicFile);
		writer.writeSingle(metadata.musicOffset);
		writer.writeString(metadata.jacketFile);
		writer.writeInt32(metadata.laneExtension);
		writer.writeInt32(metadata.isExtendedScore);
		writer.writeInt32(metadata.baseLifePoint);
	}

	void NativeScoreSerializer::readScoreEvents(Score& score, IO::BinaryReader& reader,
	                                            const ScoreVersion& version)
	{
		// time signature
		int timeSignatureCount = reader.readUInt32();
		if (timeSignatureCount)
			score.timeSignatures.clear();

		for (int i = 0; i < timeSignatureCount; ++i)
		{
			int measure = reader.readUInt32();
			int numerator = reader.readUInt32();
			int denominator = reader.readUInt32();
			score.timeSignatures[measure] = { measure, numerator, denominator };
		}

		// bpm
		int tempoCount = reader.readUInt32();
		if (tempoCount)
			score.tempoChanges.clear();

		for (int i = 0; i < tempoCount; ++i)
		{
			int tick = reader.readUInt32();
			float bpm = reader.readSingle();
			score.tempoChanges[tick] = { tick, bpm };
		}

		// hi-speed
		if (version.supportHispeed())
		{
			int hiSpeedCount = reader.readUInt32();
			id_t nextHiSpeedID = 0;
			std::set<layered_tick_t> occupied;
			for (int i = 0; i < hiSpeedCount; ++i)
			{
				int tick = std::max((int)reader.readUInt32(), 0);
				float speed = reader.readSingle();
				int layer = version.supportLayers() ? reader.readUInt32() : 0;
				float skip = version.supportHispeedSkipEase() ? reader.readSingle() : 0;
				HiSpeedEaseType ease = static_cast<HiSpeedEaseType>(
				    version.supportHispeedSkipEase() ? reader.readUInt16() : 0);
				bool hideNotes = version.supportHispeedSkipEase() ? reader.readUInt16() : false;

				// The layer doesn't exist, dropping the hispeed
				if (!isArrayIndexInBounds(layer, score.layers))
					continue;
				// Hi-speeds on the same tick are moved to the next free ticks
				while (occupied.count({ layer, tick }))
					++tick;
				occupied.insert({ layer, tick });
				HiSpeed hispeed{ tick, layer, nextHiSpeedID++, speed, skip, ease, hideNotes };
				score.hiSpeedChanges.emplace(hispeed.ID, hispeed);
			}
		}

		// skills and fever
		if (version.supportSkillFever())
		{
			int skillCount = reader.readUInt32();
			for (int i = 0; i < skillCount; ++i)
			{
				int tick = reader.readUInt32();
				SkillEffect effect = static_cast<SkillEffect>(
				    version.supportExtendedSkill() ? reader.readUInt32() : 0);
				uint8_t level =
				    version.supportExtendedSkill() ? std::clamp(reader.readUInt32(), 1u, 4u) : 1u;
				score.skills.insert(Skill{ tick, effect, level });
			}

			score.fever.startTick = reader.readUInt32();
			score.fever.endTick = reader.readUInt32();
		}
	}

	void NativeScoreSerializer::writeScoreEvents(const Score& score, IO::BinaryWriter& writer)
	{
		writer.writeInt32(score.timeSignatures.size());
		for (const auto& [_, timeSignature] : score.timeSignatures)
		{
			writer.writeInt32(timeSignature.measure);
			writer.writeInt32(timeSignature.numerator);
			writer.writeInt32(timeSignature.denominator);
		}

		writer.writeInt32(score.tempoChanges.size());
		for (const auto& [_, tempo] : score.tempoChanges)
		{
			writer.writeInt32(tempo.tick);
			writer.writeSingle(tempo.quarterPerMinute);
		}

		writer.writeInt32((int)score.hiSpeedChanges.size());
		for (const auto& [_, hiSpeed] : score.hiSpeedChanges)
		{
			writer.writeInt32(hiSpeed.tick);
			writer.writeSingle(hiSpeed.speed);
			writer.writeInt32(hiSpeed.layer);
			writer.writeSingle(hiSpeed.skips);
			writer.writeInt16(static_cast<int>(hiSpeed.ease));
			writer.writeInt16(hiSpeed.hideNotes);
		}

		writer.writeInt32(score.skills.size());
		for (const auto& skill : score.skills)
		{
			writer.writeInt32(skill.tick);
			writer.writeInt32((int)skill.effect);
			writer.writeInt32(skill.level);
		}

		writer.writeInt32(score.fever.startTick);
		writer.writeInt32(score.fever.endTick);
	}

	SerializingScore NativeScoreSerializer::deserialize(std::string filename)
	{
		id_t nextNoteID = 0, nextHoldID = 0;
		SerializingScore data;
		Score& score = data.score;
		ScoreMetadata& metadata = data.metadata;
		IO::BinaryReader reader(filename);
		if (!reader.isStreamValid())
			return data;

		std::string signature = reader.readString();
		if (signature != MMWS_SIGNATURE && signature != CC_MMWS_SIGNATURE &&
		    signature != UC_MMWS_SIGNATURE)
			throw std::runtime_error("Invalid MMWS file. Unrecognized signature");

		ScoreVersion version =
		    signature == UC_MMWS_SIGNATURE ? ScoreVersion(reader.readUInt32())
		    : signature == CC_MMWS_SIGNATURE
		        ? ScoreVersion(0, std::max((int)reader.readUInt16(), 1), (int)reader.readUInt16())
		        : ScoreVersion(0, 0, reader.readUInt32());

		if (!version.isSupportedVersion())
			throw std::runtime_error(
			    "Cannot open this file. The file was created in a newer version");

		uint32_t metadataAddress{};
		uint32_t eventsAddress{};
		uint32_t tapsAddress{};
		uint32_t holdsAddress{};
		uint32_t damagesAddress{};
		uint32_t layersAddress{};
		uint32_t waypointsAddress{};
		if (version.supportAddress())
		{
			metadataAddress = reader.readUInt32();
			eventsAddress = reader.readUInt32();
			tapsAddress = reader.readUInt32();
			holdsAddress = reader.readUInt32();
			if (version.supportDamageNote())
				damagesAddress = reader.readUInt32();
			if (version.supportLayers())
				layersAddress = reader.readUInt32();
			if (version.supportWaypoints())
				waypointsAddress = reader.readUInt32();

			reader.seek(metadataAddress);
		}

		metadata = readMetadata(reader, version);

		if (version.supportLayers())
		{
			score.layers.clear();
			reader.seek(layersAddress);

			int layerCount = reader.readUInt32();
			score.layers.reserve(layerCount);
			for (int i = 0; i < layerCount; ++i)
			{
				std::string name = reader.readString();
				float noteSpeed = version.supportForceNoteSpeed() ? reader.readSingle() : 0.0f;
				score.layers.push_back(Layer{ name, noteSpeed });
			}
		}

		if (version.supportAddress())
			reader.seek(eventsAddress);

		readScoreEvents(score, reader, version);

		if (version.supportAddress())
			reader.seek(tapsAddress);

		int noteCount = reader.readUInt32();
		score.notes.reserve(noteCount);
		for (int i = 0; i < noteCount; ++i)
		{
			Note note = readNote(LegacyNoteType::Tap, reader, version);
			if (!isArrayIndexInBounds(note.layer, score.layers))
				continue;
			note.ID = nextNoteID;
			score.notes[nextNoteID++] = note;
		}

		if (version.supportAddress())
			reader.seek(holdsAddress);

		int holdCount = reader.readUInt32();
		score.holdNotes.reserve(holdCount);
		for (int i = 0; i < holdCount; ++i)
		{
			HoldNote hold;
			HoldNoteStep holdStep;
			hold.ID = nextHoldID;

			unsigned int flags{};
			if (version.supportGuideNote())
				flags = reader.readUInt32();

			if (!version.supportExtendedNote())
			{
				holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Guide, flags & HOLD_GUIDE);
				holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Dummy, flags & HOLD_DUMMY);
			}
			else
				holdStep.flag = static_cast<HoldNoteFlag>(flags);

			Note start = readNote(LegacyNoteType::Hold, reader, version);
			bool isStartCrit = hasFlag(start.flag, NoteFlag::Critical);
			start.ID = nextNoteID++;
			start.holdID = nextHoldID;
			if (!version.supportExtendedNote())
			{
				start.ease = static_cast<EaseType>(reader.readUInt32());
				bool isHidden = (flags & HOLD_START_HIDDEN) || holdStep.isGuide();
				start.flag = setFlag(start.flag, NoteFlag::Hidden, isHidden);
				holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Critical, isStartCrit);
			}

			if (version.supportFadeType())
				hold.fadeType = static_cast<FadeType>(reader.readUInt32());
			else
				hold.fadeType = metadata.isExtendedScore ? FadeType::Out : FadeType::Classic;

			if (version.supportGuideColor())
				holdStep.guideColor = static_cast<GuideColor>(reader.readUInt32());
			else
				holdStep.guideColor = isStartCrit ? GuideColor::Yellow : GuideColor::Green;

			if (version.supportHoldLayer())
				holdStep.layer = static_cast<HoldStepLayer>(reader.readUInt32());
			else
				holdStep.layer = version.getImplicitLayer(holdStep);

			score.notes[start.ID] = start;
			hold.steps.push_back(start.ID);

			int stepCount = reader.readUInt32();
			hold.steps.reserve(stepCount + 2);
			for (int i = 0; i < stepCount; ++i)
			{
				Note mid = readNote(LegacyNoteType::HoldMid, reader, version);
				mid.ID = nextNoteID++;
				mid.holdID = nextHoldID;

				if (!version.supportExtendedNote())
				{
					switch (static_cast<EditHoldStepType>(reader.readUInt32()))
					{
					case EditHoldStepType::Hidden:
						mid.flag = setFlag(mid.flag, NoteFlag::Hidden);
						break;
					case EditHoldStepType::Skip:
						mid.flag = setFlag(mid.flag, NoteFlag::Attached);
						break;
					}
					mid.flag = setFlag(mid.flag, NoteFlag::Critical, start.flag);
					mid.ease = static_cast<EaseType>(reader.readUInt32());
				}
				score.notes[mid.ID] = mid;
				hold.steps.push_back(mid.ID);
			}

			Note end = readNote(LegacyNoteType::HoldEnd, reader, version);
			end.ID = nextNoteID++;
			end.holdID = nextHoldID;
			if (!version.supportExtendedNote())
			{
				bool isHidden = (flags & HOLD_END_HIDDEN) || holdStep.isGuide();
				end.flag = setFlag(end.flag, NoteFlag::Hidden, isHidden);
			}
			score.notes[end.ID] = end;
			hold.steps.push_back(end.ID);

			holdStep.ID = start.ID;
			hold.separators.push_back(holdStep);
			if (version.supportExtendedNote())
			{
				int separatorCount = reader.readUInt32();
				hold.separators.reserve(separatorCount + 1);
				for (int i = 0; i < separatorCount; ++i)
				{
					HoldNoteStep step;
					int index = reader.readUInt32();
					if (index < 0 || index >= hold.steps.size())
						break;
					step.ID = hold.steps.at(index);
					step.flag = static_cast<HoldNoteFlag>(reader.readUInt32());
					step.guideColor = static_cast<GuideColor>(reader.readUInt32());
					// Don't need to check supportHoldLayer here
					step.layer = static_cast<HoldStepLayer>(reader.readUInt32());
					hold.separators.push_back(step);
				}
			}

			hold.sortSteps(score.notes, !metadata.isExtendedScore);
			score.holdNotes[nextHoldID++] = hold;
		}

		if (version.supportDamageNote())
		{
			reader.seek(damagesAddress);

			int damageCount = reader.readUInt32();
			score.notes.reserve(damageCount);
			for (int i = 0; i < damageCount; ++i)
			{
				Note note = readNote(LegacyNoteType::Damage, reader, version);
				note.ID = nextNoteID;
				score.notes[nextNoteID++] = note;
			}
		}

		if (version.supportWaypoints())
		{
			score.waypoints.clear();
			reader.seek(waypointsAddress);

			int waypointCount = reader.readUInt32();
			for (int i = 0; i < waypointCount; ++i)
			{
				std::string name = reader.readString();
				int tick = reader.readUInt32();
				score.waypoints.emplace(i, Waypoint{ i, tick, name });
			}
		}

		if (!version.supportExtendedNote() && version.supportLaneExtension())
		{
			// Recalculate lane extension since it can be decreased in the previous versions
			auto reduceMaxLane = [](int lane, NoteCollection::const_reference nv) -> int
			{
				const Note& note = nv.second;
				int left = std::ceil(std::abs(note.lane - 6));
				int right = std::ceil(std::abs(note.lane + note.width - 6));
				return std::max({ left - 6, right - 6, lane });
			};
			metadata.laneExtension = std::accumulate(score.notes.begin(), score.notes.end(),
			                                         metadata.laneExtension, reduceMaxLane);
		}

		reader.close();
		return data;
	}

	void NativeScoreSerializer::serialize(const SerializingScore& data, std::string filename)
	{
		const Score& score = data.score;
		const ScoreMetadata& metadata = data.metadata;
		IO::BinaryWriter writer(filename);
		if (!writer.isStreamValid())
			return;

		// signature
		writer.writeString(UC_MMWS_SIGNATURE);

		// version
		writer.writeInt32(UC_MMWS_VERSION);

		// offsets address in order: metadata -> events -> taps -> holds
		// Cyanvas extension: -> damages -> layers -> waypoints
		uint32_t offsetsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t) * 7);

		uint32_t metadataAddress = writer.getStreamPosition();
		writeMetadata(metadata, writer);

		uint32_t eventsAddress = writer.getStreamPosition();
		writeScoreEvents(score, writer);

		uint32_t tapsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t));

		int noteCount = 0;
		for (const auto& [id, note] : score.notes)
		{
			if (note.isHold())
				continue;

			writeNote(note, writer);
			++noteCount;
		}

		uint32_t holdsAddress = writer.getStreamPosition();

		// write taps count
		writer.seek(tapsAddress);
		writer.writeInt32(noteCount);

		writer.seek(holdsAddress);
		writer.writeInt32(score.holdNotes.size());

		for (const auto& [id, hold] : score.holdNotes)
		{
			const HoldNoteStep* holdStep = &hold.separators.front();
			writer.writeInt32((int)holdStep->flag);

			// note data
			const Note& start = score.notes.at(hold.steps.front());
			writeNote(start, writer);
			writer.writeInt32((int)hold.fadeType);
			writer.writeInt32((int)holdStep->guideColor);
			writer.writeInt32((int)holdStep->layer);

			// steps
			int stepCount = hold.steps.size() - 2;
			writer.writeInt32(stepCount);
			for (auto it = std::next(hold.steps.begin()), end = std::prev(hold.steps.end());
			     it != end; ++it)
			{
				auto&& stepID = *it;
				const Note& mid = score.notes.at(stepID);
				writeNote(mid, writer);
			}

			// end
			const Note& end = score.notes.at(hold.steps.back());
			writeNote(end, writer);

			// separators
			auto begin = hold.steps.begin(), itStep = begin;
			writer.writeInt32(hold.separators.size() - 1);
			for (auto it = std::next(hold.separators.begin()), end = hold.separators.end();
			     it != end; ++it)
			{
				holdStep = &*it;
				itStep = std::find(itStep, hold.steps.end(), holdStep->ID);
				size_t index = std::distance(begin, itStep);
				if (index >= hold.steps.size())
					break;
				writer.writeInt32((int)index);
				writer.writeInt32((int)holdStep->flag);
				writer.writeInt32((int)holdStep->guideColor);
				writer.writeInt32((int)holdStep->layer);
			}
		}

		uint32_t damagesAddress = writer.getStreamPosition();
		// we don't need to write damage and tap note seperately anymore
		writer.writeInt32(0); // count

		// Cyanvas extension: write layers
		uint32_t layersAddress = writer.getStreamPosition();

		writer.seek(layersAddress);
		writer.writeInt32(score.layers.size());
		for (const auto& layer : score.layers)
		{
			writer.writeString(layer.name);
			writer.writeSingle(layer.forceNoteSpeed);
		}

		uint32_t waypointsAddress = writer.getStreamPosition();
		writer.writeInt32(score.waypoints.size());
		for (const auto& [_, waypoint] : score.waypoints)
		{
			writer.writeString(waypoint.name);
			writer.writeInt32(waypoint.tick);
		}

		// write offset addresses
		writer.seek(offsetsAddress);
		writer.writeInt32(metadataAddress);
		writer.writeInt32(eventsAddress);
		writer.writeInt32(tapsAddress);
		writer.writeInt32(holdsAddress);
		writer.writeInt32(damagesAddress);
		writer.writeInt32(layersAddress);
		writer.writeInt32(waypointsAddress);

		writer.flush();
		writer.close();
	}

	Result NativeScoreSerializer::canSerialize(const SerializingScore& data)
	{
		return Result::Ok();
	}

	void LegacyNativeScoreSerializer::writeNote(LegacyNoteType type, const Note& note,
	                                            IO::BinaryWriter& writer)
	{
		writer.writeInt32(note.tick);
		writer.writeInt32(note.lane);
		writer.writeInt32(note.width);

		if (type != LegacyNoteType::Hold && type != LegacyNoteType::HoldMid)
			writer.writeInt32(
			    (int)(note.flick >= FlickType::Down ? FlickType::Default : note.flick));

		unsigned int flags{};
		if (note.isCrit())
			flags |= NOTE_CRITICAL;
		if (note.isTrace())
			flags |= NOTE_FRICTION;
		writer.writeInt32(flags);
	}

	void LegacyNativeScoreSerializer::writeMetadata(const ScoreMetadata& metadata,
	                                                IO::BinaryWriter& writer)
	{
		writer.writeString(metadata.title);
		writer.writeString(metadata.author);
		writer.writeString(metadata.artist);
		writer.writeString(metadata.musicFile);
		writer.writeSingle(metadata.musicOffset);
		writer.writeString(metadata.jacketFile);
	}

	void LegacyNativeScoreSerializer::writeScoreEvents(const Score& score, IO::BinaryWriter& writer)
	{
		writer.writeInt32(score.timeSignatures.size());
		for (const auto& [_, timeSignature] : score.timeSignatures)
		{
			writer.writeInt32(timeSignature.measure);
			writer.writeInt32(timeSignature.numerator);
			writer.writeInt32(timeSignature.denominator);
		}

		writer.writeInt32(score.tempoChanges.size());
		for (const auto& [_, tempo] : score.tempoChanges)
		{
			writer.writeInt32(tempo.tick);
			writer.writeSingle(tempo.quarterPerMinute);
		}

		// Legacy depend on sorted hispeed
		std::vector<const HiSpeed*> hiSpeeds;
		for (const auto& [_, hispeed] : score.hiSpeedChanges)
			if (hispeed.layer == 0)
				hiSpeeds.push_back(&hispeed);
		std::sort(hiSpeeds.begin(), hiSpeeds.end(),
		          [](const HiSpeed* a, const HiSpeed* b) { return a->tick < b->tick; });
		writer.writeInt32(hiSpeeds.size());
		for (const HiSpeed* hiSpeed : hiSpeeds)
		{
			writer.writeInt32(hiSpeed->tick);
			writer.writeSingle(hiSpeed->speed);
		}

		writer.writeInt32(score.skills.size());
		for (const auto& skill : score.skills)
		{
			writer.writeInt32(skill.tick);
		}

		writer.writeInt32(score.fever.startTick);
		writer.writeInt32(score.fever.endTick);
	}

	void LegacyNativeScoreSerializer::serialize(const SerializingScore& data, std::string filename)
	{
		const Score& score = data.score;
		const ScoreMetadata& metadata = data.metadata;
		IO::BinaryWriter writer(filename);
		if (!writer.isStreamValid())
			return;

		// signature
		writer.writeString(MMWS_SIGNATURE);

		// verison
		writer.writeInt32(MMWS_VERSION);

		// offsets address in order: metadata -> events -> taps -> holds
		uint32_t offsetsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t) * 4);

		uint32_t metadataAddress = writer.getStreamPosition();
		writeMetadata(metadata, writer);

		uint32_t eventsAddress = writer.getStreamPosition();
		writeScoreEvents(score, writer);

		uint32_t tapsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t));

		int noteCount = 0;
		for (const auto& [id, note] : score.notes)
		{
			if (note.isHold())
				continue;

			writeNote(LegacyNoteType::Tap, note, writer);
			++noteCount;
		}

		uint32_t holdsAddress = writer.getStreamPosition();

		// write taps count
		writer.seek(tapsAddress);
		writer.writeInt32(noteCount);
		writer.seek(holdsAddress);

		writer.writeInt32(score.holdNotes.size());
		for (const auto& [id, hold] : score.holdNotes)
		{
			const Note& start = score.notes.at(hold.steps.front());
			const Note& end = score.notes.at(hold.steps.back());
			unsigned int flags{};
			if (hold.separators.front().isGuide())
				flags |= HOLD_GUIDE;
			if (start.isHidden())
				flags |= HOLD_START_HIDDEN;
			if (end.isHidden())
				flags |= HOLD_END_HIDDEN;
			writer.writeInt32(flags);

			// note data
			writeNote(LegacyNoteType::Hold, start, writer);
			writer.writeInt32(
			    (int)(start.ease >= EaseType::EaseInOut ? EaseType::Linear : start.ease));

			// steps
			int stepCount = hold.steps.size() - 2;
			writer.writeInt32(stepCount);
			for (auto it = std::next(hold.steps.begin()), end = std::prev(hold.steps.end());
			     it != end; ++it)
			{
				Note mid = score.notes.at(*it);
				mid.flag = setFlag(mid.flag, NoteFlag::Critical, start.flag);
				writeNote(LegacyNoteType::HoldMid, mid, writer);
				int stepType = mid.isHidden() ? 1 : mid.isAttached() ? 2 : 0;
				writer.writeInt32(stepType);
				writer.writeInt32(
				    (int)(mid.ease >= EaseType::EaseInOut ? EaseType::Linear : mid.ease));
			}

			// end
			writeNote(LegacyNoteType::HoldEnd, end, writer);
		}

		// write offset addresses
		writer.seek(offsetsAddress);
		writer.writeInt32(metadataAddress);
		writer.writeInt32(eventsAddress);
		writer.writeInt32(tapsAddress);
		writer.writeInt32(holdsAddress);

		writer.flush();
		writer.close();
	}

	Result LegacyNativeScoreSerializer::canSerialize(const SerializingScore& data)
	{
		if (data.metadata.isExtendedScore)
			return Result(ResultStatus::Error, localize(Text::exportFormatNotAvailExtend).string);
		return NativeScoreSerializer::canSerialize(data);
	}
}
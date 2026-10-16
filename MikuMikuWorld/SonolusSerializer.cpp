#include "SonolusSerializer.h"
#include "Constants.h"
#include "IO.h"
#include "File.h"
#include "Application.h"
#include "ApplicationConfiguration.h"
#include "Colors.h"

#ifdef _DEBUG
#define PRINT_DEBUG(...)                                                                           \
	do                                                                                             \
	{                                                                                              \
		fprintf(stderr, __VA_ARGS__);                                                              \
		fprintf(stderr, "\n");                                                                     \
	} while (0)
#else
#define PRINT_DEBUG(...)                                                                           \
	do                                                                                             \
	{                                                                                              \
	} while (0)
#endif // _DEBUG

using json = nlohmann::json;
using namespace Sonolus;

namespace MikuMikuWorld
{
	constexpr int UNSUPPORTED_ENUM = 2;

	void SonolusSerializer::serialize(const SerializingScore& data, std::string filename)
	{
		LevelData levelData = engine->serialize(data);
		std::string serializedData = json(levelData).dump(prettyDump ? 2 : -1);
		std::vector<uint8_t> serializedBytes(serializedData.begin(), serializedData.end());
		if (compressData)
			serializedBytes = IO::deflateGzip(serializedBytes);

		IO::File levelFile(filename, IO::FileMode::WriteBinary);
		levelFile.writeAllBytes(serializedBytes);
		levelFile.flush();
		levelFile.close();
	}

	SerializingScore SonolusSerializer::deserialize(std::string filename)
	{
		if (!IO::File::exists(filename.c_str()))
			return {};
		IO::File levelFile(filename, IO::FileMode::ReadBinary);
		std::vector<uint8_t> bytes = levelFile.readAllBytes();
		levelFile.close();
		if (IO::isGzipCompressed(bytes))
			bytes = IO::inflateGzip(bytes);
		json levelDataJson = json::parse(std::string(bytes.begin(), bytes.end()));
		LevelData levelData;
		levelDataJson.get_to(levelData);

		return engine->deserialize(levelData);
	}

#pragma region HelperFunctions

	class IdManager
	{
		inline static std::string toRef(int64_t index, int64_t base)
		{
			// Basically base 10 to base n
			if (index < 0)
				return toRef(-index, base).insert(0, "-");
			std::string ref;
			lldiv_t dv;
			do
			{
				dv = std::div(static_cast<long long>(index), static_cast<long long>(base));
				ref += dv.rem < 10 ? ('0' + dv.rem) : ('a' + (dv.rem - 10));
				index = dv.quot;
			} while (dv.quot);
			return { ref.rbegin(), ref.rend() };
		}

	  public:
		IdManager(int64_t base = 36ll, int64_t nextID = 0) : nextID(nextID), base(base) {}

		inline std::string getNextRef() { return toRef(nextID++, base); }

	  private:
		int64_t nextID, base;
	};

	double SonolusEngine::toBgmOffset(float musicOffset)
	{
		return musicOffset == 0 ? 0.0 : roundOff(-musicOffset / 1000.0);
	}

	LevelDataEntity SonolusEngine::toBpmChangeEntity(const Tempo& tempo)
	{
		return { "#BPM_CHANGE",
			     { { "#BEAT", ticksToBeats(tempo.tick) }, { "#BPM", tempo.quarterPerMinute } } };
	}

	SonolusEngine::RealType SonolusEngine::ticksToBeats(TickType ticks, TickType beatTicks)
	{
		return roundOff(static_cast<RealType>(ticks) / beatTicks);
	}

	SonolusEngine::RealType SonolusEngine::widthToSize(WidthType width)
	{
		return static_cast<RealType>(width) / 2;
	}

	SonolusEngine::RealType SonolusEngine::toSonolusLane(LaneType lane, WidthType width)
	{
		return (lane - 6) + (static_cast<RealType>(width) / 2);
	}

	float SonolusEngine::fromBgmOffset(double bgmOffset)
	{
		return bgmOffset == 0 ? 0.f : -bgmOffset * 1000;
	}

	bool SonolusEngine::fromBpmChangeEntity(const Sonolus::LevelDataEntity& bpmChangeEntity,
	                                        Tempo& tempo)
	{
		float beat;
		if (!bpmChangeEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing #BEAT key on #BPM_CHANGE");
			return false;
		}
		tempo.tick = beatsToTicks(beat);
		if (!bpmChangeEntity.tryGetDataValue("#BPM", tempo.quarterPerMinute))
		{
			PRINT_DEBUG("Missing #BPM key on #BPM_CHANGE");
			return false;
		}
		return true;
	}

	SonolusEngine::TickType SonolusEngine::beatsToTicks(RealType beats, TickType beatTicks)
	{
		return std::lround(beats * beatTicks);
	}

	SonolusEngine::WidthType SonolusEngine::sizeToWidth(RealType size) { return size * 2; }

	SonolusEngine::LaneType SonolusEngine::toNativeLane(RealType lane, RealType size)
	{
		return lane - size + 6;
	}

	inline static bool stringMatching(const std::string& source, const std::string_view& toMatch,
	                                  size_t& offset)
	{
		if (toMatch.size() > source.size() - offset)
			return false;
		std::string_view source_view = std::string_view(source).substr(offset, toMatch.size());
		bool result = source_view == toMatch;
		if (result == true)
			offset += toMatch.size();
		return result;
	}

	inline static bool stringMatchAll(const std::string& source, const std::string_view& toMatch,
	                                  size_t offset)
	{
		return std::string_view(source).substr(offset, toMatch.size()) == toMatch;
	}

	template <typename Cont, typename T, typename FGetOrder>
	inline void remapToSortedVector(const Cont& map, std::vector<const T*>& vec, FGetOrder f)
	{
		vec.reserve(vec.size() + map.size());
		std::transform(map.begin(), map.end(), std::back_inserter(vec),
		               [](const auto& kv) { return &kv.second; });
		std::sort(vec.begin(), vec.end(), [&](const T* a, const T* b) { return f(a) < f(b); });
	}

#pragma endregion

#pragma region PysekaiEngine
	LevelData PySekaiEngine::serialize(const SerializingScore& data)
	{
		const Score& score = data.score;
		const ScoreMetadata& metadata = data.metadata;
		IdManager idMgr(16);
		LevelData levelData;
		const auto getEntityName = [&](size_t idx) -> const std::string&
		{
			auto& entity = levelData.entities[idx];
			if (entity.name.empty())
				entity.name = idMgr.getNextRef();
			return entity.name;
		};
		levelData.bgmOffset = toBgmOffset(metadata.musicOffset);
		levelData.entities.reserve(score.layers.size() + score.hiSpeedChanges.size() +
		                           score.tempoChanges.size() + score.notes.size() * 2);
		auto& initEntity = levelData.entities.emplace_back("Initialization");
		initEntity.data["initialLife"] = metadata.baseLifePoint;

		for (const auto& [_, tempo] : score.tempoChanges)
			levelData.entities.emplace_back(toBpmChangeEntity(tempo));

		std::vector<size_t> groupEntIdx;
		groupEntIdx.reserve(score.layers.size());
		for (const auto& layer : score.layers)
		{
			groupEntIdx.push_back(levelData.entities.size());
			levelData.entities.emplace_back(toGroupEntity(layer));
		}

		// Leveldata is sensitive to entity ordering, we must sort before inserting them
		std::vector<const HiSpeed*> sortedSpeeds;
		remapToSortedVector(score.hiSpeedChanges, sortedSpeeds, [](const HiSpeed* hs)
		                    { return std::make_tuple(hs->tick, hs->layer, hs->ID); });

		auto speedIt = sortedSpeeds.begin();
		for (size_t i = 0; i < score.layers.size(); ++i)
		{
			size_t groupIdx = groupEntIdx[i];
			size_t lastSpeedIndex = groupIdx;
			// Hi-speeds of a layer that does not exist are not written
			while (speedIt != sortedSpeeds.end() && (*speedIt)->layer < static_cast<id_t>(i))
				++speedIt;
			for (; speedIt != sortedSpeeds.end() && (*speedIt)->layer == static_cast<id_t>(i);
			     ++speedIt)
			{
				const HiSpeed& speed = **speedIt;
				size_t newSpeedIdx = levelData.entities.size();
				auto& newSpeedEnt = levelData.entities.emplace_back(
				    toTimeScaleEntity(speed, getEntityName(groupIdx)));

				// Update reference
				if (lastSpeedIndex == groupIdx)
					levelData.entities[lastSpeedIndex].data["first"] = getEntityName(newSpeedIdx);
				else
					levelData.entities[lastSpeedIndex].data["next"] = getEntityName(newSpeedIdx);
				lastSpeedIndex = newSpeedIdx;
			}
		}

		for (auto&& skill : score.skills)
		{
			levelData.entities.emplace_back(toSkillEntity(skill));
		}

		if (score.fever.startTick >= 0 && score.fever.endTick >= score.fever.startTick)
		{
			auto&& [feverChance, feverStart] = toFeverEntities(score.fever);
			levelData.entities.emplace_back(feverChance);
			levelData.entities.emplace_back(feverStart);
		}

		std::vector<const Note*> sortedNotes;
		auto noteTuple = [](const Note* n)
		{ return std::make_tuple(n->tick, n->layer, n->lane, n->width, n->type, n->ID); };
		remapToSortedVector(score.notes, sortedNotes, noteTuple);
		std::vector<const HoldNote*> sortedHolds;
		// sort by the first note of the hold
		remapToSortedVector(score.holdNotes, sortedHolds, [&](const HoldNote* h)
		                    { return noteTuple(&score.notes.at(h->steps.front())); });

		std::multimap<TickType, size_t> simBuilder;
		for (const auto& pnote : sortedNotes)
		{
			const Note& note = *pnote;
			if (note.isHold())
				continue;
			if (note.canSimLine())
				simBuilder.emplace(note.tick, levelData.entities.size());
			levelData.entities.emplace_back(toNoteEntity(note, getSingleNoteArchetype(note),
			                                             getEntityName(groupEntIdx[note.layer])));
		}

		struct StepEntityInfo
		{
			size_t index, nearestJoint, nearestSeparator, nearestActiveHead;
			bool isJoint, isSeparator, isActiveHead, isActiveTail, isActive, isActiveCombo;
		};
		std::vector<StepEntityInfo> stepEntities;
		for (const auto& phold : sortedHolds)
		{
			const HoldNote& hold = *phold;
			stepEntities.clear();

			const HoldNoteStep* holdStep = &hold.separators.front();
			bool isActive = !holdStep->isGuide(),
			     isActiveCombo = !holdStep->isGuide() && !holdStep->isDummy(),
			     isActiveHead = isActive, isActiveTail = false, isSeparator = true;
			auto nextSeparator = std::next(hold.separators.begin());
			size_t lastEntityIndex = 0, lastJointIndex = 0, lastSeparator = 0, lastActiveHead = 0;
			for (auto&& stepID : hold.steps)
			{
				const Note& stepNote = score.notes.at(stepID);
				StepEntityInfo& info = stepEntities.emplace_back();
				if (nextSeparator != hold.separators.end() && nextSeparator->ID == stepNote.ID)
				{
					const HoldNoteStep& nextHoldStep = *nextSeparator;
					++nextSeparator;
					if (holdStep->isGuide() && !nextHoldStep.isGuide())
					{
						isActiveHead = true;
						isActive = true;
					}
					else if (!holdStep->isGuide() && nextHoldStep.isGuide())
					{
						isActiveTail = true;
						isActive = false;
					}
					holdStep = &nextHoldStep;
					isSeparator = true;
					isActiveCombo = !nextHoldStep.isDummy() && !nextHoldStep.isGuide();
				}
				else if (stepNote.ID == hold.steps.back())
				{
					isActiveTail = isActive;
					isSeparator = true;
				}

				info.index = levelData.entities.size();
				if (stepNote.canSimLine())
					simBuilder.emplace(stepNote.tick, info.index);

				LevelDataEntity& entity = levelData.entities.emplace_back(toNoteEntity(
				    stepNote, getHoldNoteArchetype(stepNote, isActiveHead, isActiveTail),
				    getEntityName(groupEntIdx[stepNote.layer]), &hold, holdStep));
				isSeparator |= (bool)entity.getDataValue<IntegerType>("isSeparator");

				if (lastEntityIndex)
					levelData.entities[lastEntityIndex].data.emplace("next",
					                                                 getEntityName(info.index));

				info.isActive = isActive;
				info.isActiveHead = isActiveHead;
				info.isActiveTail = isActiveTail;
				info.isActiveCombo = isActiveCombo;
				info.isSeparator = isSeparator;
				info.isJoint = !stepNote.isAttached();
				info.nearestJoint = lastJointIndex =
				    !stepNote.isAttached() ? info.index : lastJointIndex;
				info.nearestSeparator = lastSeparator = isSeparator ? info.index : lastSeparator;
				info.nearestActiveHead = lastActiveHead =
				    isActiveHead ? info.index : lastActiveHead;
				isActiveHead = isActiveTail = isSeparator = false;
				lastEntityIndex = info.index;
			}

			// Do backward scan, while using stored 'nearestJoint' to find the attachedHead
			for (auto tailIt = stepEntities.rbegin(), stepIt = std::next(tailIt);
			     stepIt != stepEntities.rend(); ++stepIt)
			{
				if (stepIt->isJoint)
				{
					tailIt = stepIt;
					continue;
				}
				auto& attachEntity = levelData.entities[stepIt->index];
				size_t headEntityIdx = stepIt->nearestJoint;
				size_t tailEntityIdx = tailIt->index;
				attachEntity.data.emplace("attachHead", getEntityName(headEntityIdx));
				attachEntity.data.emplace("attachTail", getEntityName(tailEntityIdx));
				estimateAttachEntity(attachEntity, levelData.entities[headEntityIdx],
				                     levelData.entities[tailEntityIdx]);
			}

			auto isStepTail = [](const StepEntityInfo& i) { return i.isActiveTail; };
			for (auto tailIt = std::find_if(stepEntities.rbegin(), stepEntities.rend(), isStepTail);
			     tailIt != stepEntities.rend();
			     tailIt = std::find_if(std::next(tailIt), stepEntities.rend(), isStepTail))
			{
				auto& tailEntity = levelData.entities[tailIt->index];
				tailEntity.data.emplace("activeHead", getEntityName(tailIt->nearestActiveHead));
			}

			size_t lastActiveTail;
			bool jointInsertCombo = false;
			for (auto tailIt = stepEntities.rbegin(), headIt = std::next(tailIt);
			     headIt != stepEntities.rend(); ++headIt)
			{
				if (!headIt->isJoint && !headIt->isSeparator)
					continue;
				if (tailIt->isActiveTail)
					lastActiveTail = tailIt->index;

				if (headIt->isActiveCombo && !jointInsertCombo)
				{
					// Make sure these have proper name
					getEntityName(headIt->nearestJoint);
					getEntityName(lastJointIndex);
					insertTransientTickNote(headIt->nearestJoint, lastJointIndex,
					                        headIt->isActiveHead, levelData.entities);
					jointInsertCombo = true;
				}

				levelData.entities.push_back({ "Connector" });
				auto& data = levelData.entities.back().data;
				data.emplace("head", getEntityName(headIt->index));
				data.emplace("tail", getEntityName(tailIt->index));
				if (headIt->isActive)
				{
					data.emplace("activeHead", getEntityName(headIt->nearestActiveHead));
					data.emplace("activeTail", getEntityName(lastActiveTail));
				}
				data.emplace("segmentHead", getEntityName(headIt->nearestSeparator));
				data.emplace("segmentTail", getEntityName(lastSeparator));

				if (headIt->isJoint)
				{
					lastJointIndex = headIt->index;
					jointInsertCombo = false;
				}
				if (headIt->isSeparator)
					lastSeparator = headIt->index;
				if (headIt->isActiveHead)
					lastActiveHead = headIt->index;
				tailIt = headIt;
			}
		}

		std::vector<size_t> simEntities;
		for (auto it = simBuilder.begin(), end = simBuilder.end(); it != end;)
		{
			auto [startEnt, endEnt] = simBuilder.equal_range(it->first);
			simEntities.clear();
			std::transform(startEnt, endEnt, std::back_inserter(simEntities),
			               [](const std::multimap<TickType, size_t>::value_type& val)
			               { return val.second; });
			std::sort(simEntities.begin(), simEntities.end(),
			          [&](size_t aEnt, size_t bEnt)
			          {
				          return levelData.entities[aEnt].getDataValue<RealType>("lane") <
				                 levelData.entities[bEnt].getDataValue<RealType>("lane");
			          });

			for (size_t i = 1; i < simEntities.size(); ++i)
			{
				levelData.entities.push_back({ "SimLine",
				                               { { "left", getEntityName(simEntities[i - 1]) },
				                                 { "right", getEntityName(simEntities[i]) } } });
			}
			it = endEnt;
		}

		return levelData;
	}

	SerializingScore PySekaiEngine::deserialize(const LevelData& levelData)
	{
		size_t errorCount = 0, unsupportedCount = 0;
		id_t nextNoteID = 0, nextHoldID = 0;
		SerializingScore data;
		Score& score = data.score;
		ScoreMetadata& metadata = data.metadata;
		metadata.musicOffset = fromBgmOffset(levelData.bgmOffset);
		const auto isInitEntity = [](const Sonolus::LevelDataEntity& e)
		{ return e.archetype == "Initialization"; };
		const auto isBpmChangeEntity = [](const LevelDataEntity& e)
		{ return e.archetype == "#BPM_CHANGE"; };
		const auto isTimescaleGroupEntity = [](const LevelDataEntity& e)
		{ return e.archetype == "#TIMESCALE_GROUP"; };
		const auto isNoteEntity = [](const Sonolus::LevelDataEntity& e)
		{
			return IO::endsWith(e.archetype, "Note") &&
			       e.archetype.find("Transient") == std::string::npos;
		};
		const auto isSkillEntity = [](const Sonolus::LevelDataEntity& e)
		{ return e.archetype == "Skill"; };
		const auto isFeverEntity = [](const Sonolus::LevelDataEntity& e)
		{ return e.archetype == "FeverChance" || e.archetype == "FeverStart"; };

		std::unordered_map<RefType, size_t> entityNameMap;
		entityNameMap.reserve(levelData.entities.size());
		std::vector<size_t> noteEntities;
		std::unordered_map<RefType, size_t> notePrevMap;
		for (size_t i = 0; i < levelData.entities.size(); ++i)
		{
			const auto& entity = levelData.entities[i];
			if (!entity.name.empty())
				entityNameMap.emplace(entity.name, i);
			if (isNoteEntity(entity))
			{
				noteEntities.push_back(i);
				RefType nextNoteEntity;
				if (entity.tryGetDataValue("next", nextNoteEntity))
					notePrevMap.emplace(nextNoteEntity, i);
			}
		}

		score.tempoChanges.clear();
		for (const auto& bpmChangeEntity : levelData.entities)
		{
			if (!isBpmChangeEntity(bpmChangeEntity))
				continue;
			Tempo tempo;
			if (fromBpmChangeEntity(bpmChangeEntity, tempo))
				score.tempoChanges.emplace(tempo.tick, tempo);
			else
				errorCount++;
		}
		if (score.tempoChanges.empty())
			score.tempoChanges.emplace(0, Tempo{});

		score.layers.clear();
		score.hiSpeedChanges.clear();
		id_t nextHiSpeedID = 0;
		std::set<layered_tick_t> occupiedHispeed;
		std::unordered_map<RefType, size_t> groupNameMap;
		for (const auto& groupEntity : levelData.entities)
		{
			if (!isTimescaleGroupEntity(groupEntity))
				continue;

			size_t layerIdx = score.layers.size();
			Layer& layer =
			    score.layers.emplace_back(layerIdx ? "#" + std::to_string(layerIdx) : "default");
			if (!fromGroupEntity(groupEntity, layer))
			{
				errorCount++;
				continue;
			}
			RefType nextSpeedChange;
			groupEntity.tryGetDataValue("first", nextSpeedChange);
			if (groupEntity.name.size())
				groupNameMap.emplace(groupEntity.name, layerIdx);

			while (!nextSpeedChange.empty())
			{
				auto it = entityNameMap.find(nextSpeedChange);
				if (it == entityNameMap.end())
				{
					PRINT_DEBUG("#TIMESCALE_CHANGE entity not found!");
					errorCount++;
					break;
				}
				HiSpeed hispeed;
				if (!fromTimeScaleEntity(levelData.entities[it->second], groupEntity, hispeed,
				                         groupNameMap))
				{
					errorCount++;
					continue;
				}
				hispeed.layer = layerIdx;
				// Discard duplicate hispeeds occupying the same tick on a layer
				if (occupiedHispeed.insert({ hispeed.layer, hispeed.tick }).second)
				{
					hispeed.ID = nextHiSpeedID++;
					score.hiSpeedChanges.emplace(hispeed.ID, hispeed);
				}

				nextSpeedChange.clear();
				levelData.entities[it->second].tryGetDataValue("next", nextSpeedChange);
			}
		}
		if (score.layers.empty())
			score.layers.emplace_back(Layer{ "default", 0 });

		for (const auto& entity : levelData.entities)
		{
			if (isInitEntity(entity))
			{
				entity.tryGetDataValue("initialLife", metadata.baseLifePoint);
			}
			else if (isSkillEntity(entity))
			{
				Skill skill;
				if (fromSkillEntity(entity, skill))
					score.skills.insert(skill);
				else
					errorCount++;
			}
			else if (isFeverEntity(entity))
			{
				if (!fromFeverEntity(entity, score.fever))
					errorCount++;
			}
		}

		const auto hasParentNote = [&](const LevelDataEntity& e)
		{ return e.name.size() && notePrevMap.find(e.name) != notePrevMap.end(); };
		for (const auto& entIdx : noteEntities)
		{
			auto& tapNoteEntity = levelData.entities[entIdx];
			if (tapNoteEntity.dataExists("next") || hasParentNote(tapNoteEntity))
				continue;
			Note note;
			int status = fromTapNoteEntity(tapNoteEntity, note, groupNameMap);
			if (status == 0)
			{
				errorCount++;
				continue;
			}
			else if (status == UNSUPPORTED_ENUM)
				unsupportedCount++;
			note.ID = nextNoteID++;
			score.notes.emplace(note.ID, note);
		}

		std::vector<Note> holdNotes;
		for (const auto& entIdx : noteEntities)
		{
			auto& holdStartEntity = levelData.entities[entIdx];
			if (!holdStartEntity.dataExists("next") || hasParentNote(holdStartEntity))
				continue;
			size_t nextEntIndex = entIdx;
			bool validHold = true;
			holdNotes.clear();
			HoldNote hold;
			hold.fadeType = FadeType::Custom;
			HoldNoteStep* holdStep = nullptr;
			while (true)
			{
				RefType next;
				auto& noteEntity = levelData.entities[nextEntIndex];
				bool isSeparator = !holdStep || noteEntity.getDataValue<IntegerType>("isSeparator");

				Note& stepNote = holdNotes.emplace_back();
				stepNote.type = NoteType::Tick;
				int status = fromHoldMidEntity(noteEntity, stepNote, groupNameMap);
				if (status == 0)
				{
					validHold = false;
					break;
				}
				else if (status == UNSUPPORTED_ENUM)
				{
					unsupportedCount++;
				}
				if (isSeparator)
				{
					holdStep = &hold.separators.emplace_back();
					holdStep->ID = holdNotes.size() - 1; // using step index as temporary ids
					int kind = 0;
					if (noteEntity.tryGetDataValue("segmentKind", kind))
					{
						if (!fromSegmentNumeric(kind, *holdStep))
						{
							PRINT_DEBUG("Unsupported segmentKind %d", kind);
							validHold = false;
							break;
						}
					}
					else
					{
						PRINT_DEBUG("Hold note doesn't have a valid segmentKind!");
						validHold = false;
						break;
					}
					int layer = 0;
					if (noteEntity.tryGetDataValue("segmentLayer", layer))
					{
						holdStep->layer = fromLayerNumeric(layer);
						if (holdStep->layer == HoldStepLayer::LayerCount)
						{
							unsupportedCount++;
							holdStep->layer = HoldStepLayer::Top;
						}
					}
				}
				if (!noteEntity.tryGetDataValue("next", next))
					break;
				auto it = entityNameMap.find(next);
				if (it == entityNameMap.end())
				{
					PRINT_DEBUG("Entity named '%s' not found!", next.c_str());
					validHold = false;
					break;
				}
				nextEntIndex = it->second;
			}
			if (!validHold || !isValidHoldNotes(holdNotes, hold))
			{
				errorCount++;
				continue;
			}

			// Start and end note are implicitly tap note
			if (holdNotes.front().isHidden())
				holdNotes.front().type = NoteType::Tap;
			if (holdNotes.back().isHidden())
				holdNotes.back().type = NoteType::Tap;
			// Assign valid ids
			for (auto& note : holdNotes)
			{
				note.ID = nextNoteID++;
				note.holdID = nextHoldID;
				hold.steps.push_back(note.ID);
				score.notes.emplace(note.ID, note);
			}
			for (auto& step : hold.separators)
			{
				step.ID = holdNotes[step.ID].ID;
			}
			hold.ID = nextHoldID;
			HoldNote& insertedHold =
			    score.holdNotes.emplace(nextHoldID++, std::move(hold)).first->second;
			insertedHold.sortSteps(score.notes);
		}

		auto reduceMaxLane = [](int lane, const std::pair<id_t, Note>& noteP) -> int
		{
			const Note& note = noteP.second;
			int left = std::ceil(std::abs(note.lane - 6));
			int right = std::ceil(std::abs(note.lane + note.width - 6));
			return std::max({ left - 6, right - 6, lane });
		};

		metadata.laneExtension =
		    std::accumulate(score.notes.begin(), score.notes.end(), 0, reduceMaxLane);

		std::string message;
		if (unsupportedCount)
		{
			PRINT_DEBUG("Total of %zd unsupported feature(s) found", unsupportedCount);
			message += "Score contains unsupported features!\n\n";
		}
		if (errorCount)
		{
			PRINT_DEBUG("Total of %zd error(s) found", errorCount);
			message += IO::formatString("%s\n%s", localize(Text::errorLoadScoreFile),
			                            localize(Text::scorePartiallyMissing));
		}
		if (errorCount || unsupportedCount)
			throw PartialScoreDeserializeError(data, message);
		return data;
	}

	bool PySekaiEngine::canSerialize(const Score& score) { return true; }

	LevelDataEntity PySekaiEngine::toGroupEntity(const Layer& layer)
	{
		return { "#TIMESCALE_GROUP",
			     { { "editorName", layer.name }, { "forceNoteSpeed", layer.forceNoteSpeed } } };
	}

	LevelDataEntity PySekaiEngine::toTimeScaleEntity(const HiSpeed& hispeed,
	                                                 const RefType& groupName)
	{
		return { "#TIMESCALE_CHANGE",
			     { { "#BEAT", ticksToBeats(hispeed.tick) },
			       { "#TIMESCALE", roundOff(hispeed.speed) },
			       { "#TIMESCALE_SKIP", hispeed.skips },
			       { "#TIMESCALE_EASE", static_cast<IntegerType>(hispeed.ease) },
			       { "#TIMESCALE_GROUP", groupName },
			       { "hideNotes", static_cast<IntegerType>(hispeed.hideNotes) },
			       { "editorLane", toSonolusLane(-hispeed.layer, 0) } } };
	}

	Sonolus::LevelDataEntity PySekaiEngine::toSkillEntity(const Skill& skill)
	{
		return { "Skill",
			     { { "#BEAT", ticksToBeats(skill.tick) },
			       { "effect", static_cast<IntegerType>(skill.effect) },
			       { "level", static_cast<IntegerType>(skill.level) } } };
	}

	std::pair<Sonolus::LevelDataEntity, Sonolus::LevelDataEntity>
	PySekaiEngine::toFeverEntities(const Fever& fever)
	{
		return { { "FeverChance", { { "#BEAT", ticksToBeats(fever.startTick) } } },
			     { "FeverStart", { { "#BEAT", ticksToBeats(fever.endTick) } } } };
	}

	LevelDataEntity PySekaiEngine::toNoteEntity(const Note& note, const std::string& archetype,
	                                            const RefType& groupName, const HoldNote* hold,
	                                            const HoldNoteStep* holdStep)
	{
		HoldStepLayer stepLayer = HoldStepLayer::Top;
		bool isSeparator = false;
		if (holdStep && hold)
		{
			isSeparator = (holdStep->ID == note.ID) ||
			              (holdStep->isGuide() && hold->fadeType == FadeType::Classic &&
			               hold->steps.back() != note.ID && hold->steps.front() != note.ID);
			stepLayer = holdStep->layer;
		}
		return { archetype,
			     { { "#TIMESCALE_GROUP", groupName },
			       { "#BEAT", ticksToBeats(note.tick) },
			       { "lane", toSonolusLane(note.lane, note.width) },
			       { "size", widthToSize(note.width) },
			       { "direction", toDirectionNumeric(note.flick) },
			       { "isAttached", note.isAttached() },
			       { "isSeparator", static_cast<IntegerType>(isSeparator) },
			       { "connectorEase", toEaseNumeric(note.ease) },
			       { "segmentKind", toSegmentNumeric(holdStep) },
			       { "segmentAlpha", roundOff(note.guideAlpha) },
			       { "segmentLayer", toLayerNumeric(stepLayer) },
			       { "effectKind", toEffectNumeric(note.soundEffect) } } };
	}

	std::string PySekaiEngine::getSingleNoteArchetype(const Note& note)
	{
		std::string archetype = (note.isDummy() ? "Fake" : "");
		if (note.isHidden())
			archetype += "Anchor";
		else
		{
			switch (note.type)
			{
			case NoteType::Tap:
				archetype += (note.isCrit() ? "Critical" : "Normal");
				if (note.isTrace())
					archetype += "Trace";
				if (note.isFlick())
					archetype += "Flick";
				if (!note.isTrace() && !note.isFlick())
					archetype += "Tap";
				break;
			case NoteType::Tick:
				archetype += (note.isCrit() ? "Critical" : "Normal");
				archetype += "Tick";
				break;
			case NoteType::Damage:
				archetype += "Damage";
				break;
			default:
				PRINT_DEBUG("Unknown tap note");
				archetype += "Anchor";
				break;
			}
		}

		archetype += "Note";
		return archetype;
	}

	std::string PySekaiEngine::getHoldNoteArchetype(const Note& note, bool isHead, bool isTail)
	{
		std::string archetype = (note.isDummy() ? "Fake" : "");
		if (note.isHidden())
			archetype += "Anchor";
		else
		{
			switch (note.type)
			{
			case NoteType::Tap:
			{
				archetype += (note.isCrit() ? "Critical" : "Normal");
				if (isHead)
					archetype += "Head";
				if (isTail)
					archetype += "Tail";
				if (note.isTrace())
					archetype += "Trace";
				if (note.isFlick())
					archetype += "Flick";
				if (!note.isTrace() && !note.isFlick())
					archetype += !isTail ? "Tap" : "Release";
				break;
			}
			case NoteType::Tick:
				archetype += (note.isCrit() ? "Critical" : "Normal");
				archetype += "Tick";
				break;
			case NoteType::Damage:
				archetype += "Damage";
				break;
			default:
				PRINT_DEBUG("Unknown tap note");
				archetype += "Anchor";
				break;
			}
		}
		archetype += "Note";
		return archetype;
	}

	void PySekaiEngine::insertTransientTickNote(size_t headIndex, size_t tailIndex,
	                                            bool isActiveHead,
	                                            std::vector<Sonolus::LevelDataEntity>& entities)
	{
		double headHalfBeat;
		double headFracHalfBeat =
		    std::modf(entities[headIndex].getDataValue<RealType>("#BEAT") * 2, &headHalfBeat);
		bool skips = (isActiveHead || headFracHalfBeat != 0) ? 1 : 0;
		int endHalfBeat = std::ceil(entities[tailIndex].getDataValue<RealType>("#BEAT") * 2);
		for (int halfBeat = headHalfBeat + skips; halfBeat < endHalfBeat; ++halfBeat)
		{
			entities.push_back({ "TransientHiddenTickNote",
			                     { { "#BEAT", halfBeat / 2. },
			                       { "isAttached", 1 },
			                       { "attachHead", entities[headIndex].name },
			                       { "attachTail", entities[tailIndex].name } } });
		}
	}

	void PySekaiEngine::estimateAttachEntity(Sonolus::LevelDataEntity& attach,
	                                         const Sonolus::LevelDataEntity& head,
	                                         const Sonolus::LevelDataEntity& tail)
	{
		int easeNumeric;
		RealType headBeat, headLane, headSize, tailBeat, tailLane, tailSize, attachBeat;
		if (!head.tryGetDataValue("#BEAT", headBeat) || !head.tryGetDataValue("lane", headLane) ||
		    !head.tryGetDataValue("size", headSize) || !tail.tryGetDataValue("#BEAT", tailBeat) ||
		    !tail.tryGetDataValue("lane", tailLane) || !tail.tryGetDataValue("size", tailSize) ||
		    !head.tryGetDataValue("connectorEase", easeNumeric) ||
		    !attach.tryGetDataValue("#BEAT", attachBeat))
			return;
		RealType ratio = unlerpD(headBeat, tailBeat, attachBeat);
		float (*easeFunc)(float, float, float);
		switch (easeNumeric)
		{
		default:
		case 1:
			easeFunc = lerp;
			break;
		case 2:
			easeFunc = easeIn;
			break;
		case 3:
			easeFunc = easeOut;
			break;
		case 4:
			easeFunc = easeInOut;
			break;
		case 5:
			easeFunc = easeOutIn;
			break;
		}
		attach.data["lane"] = easeFunc(headLane, tailLane, ratio);
		attach.data["size"] = easeFunc(headSize, tailSize, ratio);
	}

	int PySekaiEngine::toDirectionNumeric(FlickType flick)
	{
		switch (flick)
		{
		case FlickType::Left:
			return 1;
		case FlickType::Right:
			return 2;
		case FlickType::Down:
			return 3;
		case FlickType::DownLeft:
			return 4;
		case FlickType::DownRight:
			return 5;
		default:
			PRINT_DEBUG("Unknown FlickType");
			[[fallthrough]];
		case FlickType::None:
		case FlickType::Default:
			return 0;
		}
	}

	int PySekaiEngine::toEaseNumeric(EaseType ease)
	{
		switch (ease)
		{
		case EaseType::EaseNone:
			return 0;
		case EaseType::Linear:
			return 1;
		case EaseType::EaseIn:
			return 2;
		case EaseType::EaseOut:
			return 3;
		case EaseType::EaseInOut:
			return 4;
		case EaseType::EaseOutIn:
			return 5;
		default:
			PRINT_DEBUG("Unknown EaseType");
			return 1;
		}
	}

	int PySekaiEngine::toEffectNumeric(SoundEffectType effect)
	{
		switch (effect)
		{
		case SoundEffectType::None:
			return 1;
		case SoundEffectType::TapPerfect:
			return 2;
		case SoundEffectType::Flick:
			return 3;
		case SoundEffectType::Trace:
			return 4;
		case SoundEffectType::Tick:
			return 5;
		case SoundEffectType::CritTap:
			return 6;
		case SoundEffectType::CritFlick:
			return 7;
		case SoundEffectType::CritTrace:
			return 8;
		case SoundEffectType::CritTick:
			return 9;
		case SoundEffectType::Damage:
			return 10;
		default:
			PRINT_DEBUG("Unknown SoundEffectType");
		case SoundEffectType::Default:
			return 0;
		}
	}

	int PySekaiEngine::toSegmentNumeric(const HoldNoteStep* holdStep)
	{
		if (!holdStep)
			return 1;
		if (!holdStep->isGuide())
		{
			return (!holdStep->isCrit() ? 1 : 2) + (!holdStep->isDummy() ? 0 : 50);
		}
		else
		{
			switch (holdStep->guideColor)
			{
			case GuideColor::Neutral:
				return 101;
			case GuideColor::Red:
				return 102;
			case GuideColor::Green:
				return 103;
			case GuideColor::Blue:
				return 104;
			case GuideColor::Yellow:
				return 105;
			case GuideColor::Purple:
				return 106;
			case GuideColor::Cyan:
				return 107;
			case GuideColor::Black:
				return 108;
			default:
				PRINT_DEBUG("Unknown segment kind");
				return 0;
			}
		}
	}

	int PySekaiEngine::toLayerNumeric(const HoldStepLayer& layer)
	{
		switch (layer)
		{
		case HoldStepLayer::Top:
			return 0;
		case HoldStepLayer::Bottom:
			return 1;
		default:
			PRINT_DEBUG("Unknown segment layer");
			return 0;
		}
	}

	bool PySekaiEngine::fromGroupEntity(const LevelDataEntity& groupEntity, Layer& layer)
	{
		groupEntity.tryGetDataValue("editorName", layer.name);
		groupEntity.tryGetDataValue("forceNoteSpeed", layer.forceNoteSpeed);
		if (!isWithinRange(layer.forceNoteSpeed, 1, 12))
			layer.forceNoteSpeed = 0.0f;
		return true;
	}

	bool PySekaiEngine::fromTimeScaleEntity(const LevelDataEntity& timescaleEntity,
	                                        const LevelDataEntity& groupEntity, HiSpeed& hispeed,
	                                        std::unordered_map<RefType, size_t>& groupNameMap)
	{
		if (timescaleEntity.archetype != "#TIMESCALE_CHANGE")
		{
			PRINT_DEBUG("Mismatch entity archetype. Expected #TIMESCALE_CHANGE got '%s' instead",
			            timescaleEntity.archetype.c_str());
			return false;
		}
		std::string groupName;
		float beat;
		int easing = 0;
		if (!timescaleEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing #BEAT key on #TIMESCALE_CHANGE");
			return false;
		}
		hispeed.tick = beatsToTicks(beat);
		if (!timescaleEntity.tryGetDataValue("#TIMESCALE", hispeed.speed))
		{
			PRINT_DEBUG("Missing #TIMESCALE key on #TIMESCALE_CHANGE");
			return false;
		}
		if (!timescaleEntity.tryGetDataValue("#TIMESCALE_GROUP", groupName))
		{
			PRINT_DEBUG("Missing #TIMESCALE_GROUP key on #TIMESCALE_CHANGE");
			return false;
		}
		if (groupName != groupEntity.name)
		{
			PRINT_DEBUG("#TIMESCALE_CHANGE of group '%s' referencing group '%s'",
			            groupEntity.name.c_str(), groupName.c_str());
			return false;
		}
		timescaleEntity.tryGetDataValue("#TIMESCALE_SKIP", hispeed.skips);
		if (timescaleEntity.tryGetDataValue("#TIMESCALE_EASE", easing))
		{
			if (easing < 0 || easing >= static_cast<int>(HiSpeedEaseType::EaseTypeCount))
			{
				PRINT_DEBUG("Unknown ease type for hispeed!");
				return false;
			}
			hispeed.ease = static_cast<HiSpeedEaseType>(easing);
		}
		timescaleEntity.tryGetDataValue("hideNotes", hispeed.hideNotes);
		return true;
	}

	bool PySekaiEngine::fromSkillEntity(const LevelDataEntity& skillEntity, Skill& skill)
	{
		RealType beat;
		IntegerType effect = 0, level = 1;
		if (!skillEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing '#BEAT' key on %s (%s)", skillEntity.archetype.c_str(),
			            skillEntity.name.c_str());
			return false;
		}
		skillEntity.tryGetDataValue("effect", effect);
		skillEntity.tryGetDataValue("level", level);
		skill.tick = beatsToTicks(beat);
		skill.effect = static_cast<SkillEffect>(effect);
		skill.level = level;
		return true;
	}

	bool PySekaiEngine::fromFeverEntity(const LevelDataEntity& feverEntity, Fever& fever)
	{
		RealType beat;
		if (!feverEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing '#BEAT' key on %s (%s)", feverEntity.archetype.c_str(),
			            feverEntity.name.c_str());
			return false;
		}
		if (feverEntity.archetype == "FeverChance")
			fever.startTick = beatsToTicks(beat);
		else
			fever.endTick = beatsToTicks(beat);
		return true;
	}

	int PySekaiEngine::fromNoteEntity(const LevelDataEntity& noteEntity, Note& note,
	                                  const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		int status = true;
		RealType beat, lane, size;
		std::string group;
		if (!noteEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing #BEAT key on %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (!noteEntity.tryGetDataValue("lane", lane))
		{
			PRINT_DEBUG("Missing 'lane' key on %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (!noteEntity.tryGetDataValue("size", size))
		{
			PRINT_DEBUG("Missing 'size' key on %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		note.tick = beatsToTicks(beat);
		note.width = sizeToWidth(size);
		note.lane = toNativeLane(lane, size);
		note.layer = 0;
		if (noteEntity.tryGetDataValue("#TIMESCALE_GROUP", group))
		{
			auto it = groupNameMap.find(group);
			if (it == groupNameMap.end())
			{
				PRINT_DEBUG("%s (%s) referencing non group '%s'", noteEntity.archetype.c_str(),
				            noteEntity.name.c_str(), group.c_str());
				return false;
			}
			note.layer = it->second;
		}
		IntegerType effect = 0;
		noteEntity.tryGetDataValue("effectKind", effect);
		note.soundEffect = fromEffectNumeric(effect);
		if (note.soundEffect == SoundEffectType::SoundEffectTypeCount)
		{
			PRINT_DEBUG("Unknown effect value %d!", effect);
			note.soundEffect = SoundEffectType::Default;
			status = UNSUPPORTED_ENUM;
		}
		if (!isValidNoteState(note))
		{
			PRINT_DEBUG("Invalid note state (t:%d, w:%f, l:%f) on %s (%s)", note.tick, note.width,
			            note.lane, noteEntity.archetype.c_str(), noteEntity.name.c_str());
			return false;
		}
		return status;
	}

	int PySekaiEngine::fromTapNoteEntity(const LevelDataEntity& tapNoteEntity, Note& note,
	                                     const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		int status = fromNoteEntity(tapNoteEntity, note, groupNameMap);
		if (status == 0)
			return false;

		size_t offset = 0;
		if (stringMatching(tapNoteEntity.archetype, "Fake", offset))
			note.flag = setFlag(note.flag, NoteFlag::Dummy);
		if (stringMatchAll(tapNoteEntity.archetype, "AnchorNote", offset))
		{
			note.flag = setFlag(note.flag, NoteFlag::Hidden);
			return true;
		}
		if (stringMatchAll(tapNoteEntity.archetype, "DamageNote", offset))
		{
			note.type = NoteType::Damage;
			return true;
		}
		if (stringMatching(tapNoteEntity.archetype, "Critical", offset))
			note.flag = setFlag(note.flag, NoteFlag::Critical);
		else if (!stringMatching(tapNoteEntity.archetype, "Normal", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", tapNoteEntity.archetype.c_str(),
			            tapNoteEntity.name.c_str());
			return false;
		}
		if (stringMatchAll(tapNoteEntity.archetype, "TickNote", offset))
		{
			note.type = NoteType::Tick;
			return true;
		}
		note.type = NoteType::Tap;
		bool hasModifier = false;
		stringMatching(tapNoteEntity.archetype, "Head", offset) ||
		    stringMatching(tapNoteEntity.archetype, "Tail", offset);
		if (stringMatching(tapNoteEntity.archetype, "Trace", offset))
		{
			hasModifier = true;
			note.flag = setFlag(note.flag, NoteFlag::Trace);
		}
		if (stringMatching(tapNoteEntity.archetype, "Flick", offset))
		{
			hasModifier = true;
			int direction;
			if (!tapNoteEntity.tryGetDataValue("direction", direction))
			{
				PRINT_DEBUG("Missing 'direction' key on %s (%s)", tapNoteEntity.archetype.c_str(),
				            tapNoteEntity.name.c_str());
				return false;
			}
			note.flick = fromDirectionNumeric(direction);
			if (note.flick == FlickType::FlickTypeCount)
			{
				PRINT_DEBUG("Unknown direction value %d!", direction);
				note.flick = FlickType::None;
				status = UNSUPPORTED_ENUM;
			}
		}
		if ((!hasModifier && !stringMatching(tapNoteEntity.archetype, "Tap", offset) &&
		     !stringMatching(tapNoteEntity.archetype, "Release", offset)) ||
		    !stringMatchAll(tapNoteEntity.archetype, "Note", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", tapNoteEntity.archetype.c_str(),
			            tapNoteEntity.name.c_str());
			return false;
		}
		return status;
	}

	int PySekaiEngine::fromHoldMidEntity(const Sonolus::LevelDataEntity& noteEntity, Note& note,
	                                     const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		int status = fromTapNoteEntity(noteEntity, note, groupNameMap);
		if (status == 0)
			return false;

		noteEntity.tryGetDataValue("segmentAlpha", note.guideAlpha);
		int attached = 0;
		if (noteEntity.tryGetDataValue("isAttached", attached) && attached)
			note.flag = setFlag(note.flag, NoteFlag::Attached);

		int ease;
		if (noteEntity.tryGetDataValue("connectorEase", ease))
		{
			note.ease = fromEaseNumeric(ease);
			if (note.ease == EaseType::EaseTypeCount)
			{
				PRINT_DEBUG("Unknown connectorEase %d", ease);
				note.ease = EaseType::Linear;
				status = UNSUPPORTED_ENUM;
			}
		}

		return status;
	}

	bool PySekaiEngine::isValidNoteState(const Note& note)
	{
		return note.tick >= 0 && note.width >= 0;
	}

	bool PySekaiEngine::isValidHoldNotes(const std::vector<Note>& holdNotes, const HoldNote& hold)
	{
		if (holdNotes.size() < 2)
		{
			PRINT_DEBUG("Hold notes missing start/end");
			return false;
		}
		return true;
	}

	FlickType PySekaiEngine::fromDirectionNumeric(int direction)
	{
		switch (direction)
		{
		case 0:
			return FlickType::Default;
		case 1:
			return FlickType::Left;
		case 2:
			return FlickType::Right;
		case 3:
			return FlickType::Down;
		case 4:
			return FlickType::DownLeft;
		case 5:
			return FlickType::DownRight;
		default:
			return FlickType::FlickTypeCount;
		}
	}

	EaseType PySekaiEngine::fromEaseNumeric(int ease)
	{
		switch (ease)
		{
		case 0:
			return EaseType::EaseNone;
		case 1:
			return EaseType::Linear;
		case 2:
			return EaseType::EaseIn;
		case 3:
			return EaseType::EaseOut;
		case 4:
			return EaseType::EaseInOut;
		case 5:
			return EaseType::EaseOutIn;
		default:
			return EaseType::EaseTypeCount;
		}
	}

	SoundEffectType PySekaiEngine::fromEffectNumeric(int effectKind)
	{
		switch (effectKind)
		{
		case 0:
			return SoundEffectType::Default;
		case 1:
			return SoundEffectType::None;
		case 2:
			return SoundEffectType::TapPerfect;
		case 3:
			return SoundEffectType::Flick;
		case 4:
			return SoundEffectType::Trace;
		case 5:
			return SoundEffectType::Tick;
		case 6:
			return SoundEffectType::CritTap;
		case 7:
			return SoundEffectType::CritFlick;
		case 8:
			return SoundEffectType::CritTrace;
		case 9:
			return SoundEffectType::CritTick;
		case 10:
			return SoundEffectType::Damage;
		default:
			return SoundEffectType::SoundEffectTypeCount;
		}
	}

	static bool isGuideKind(int kind) { return 101 <= kind && kind <= 108; }
	bool PySekaiEngine::fromSegmentNumeric(int segmentKind, HoldNoteStep& holdStep)
	{
		if (isGuideKind(segmentKind))
		{
			holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Guide);
			holdStep.guideColor = static_cast<GuideColor>(segmentKind - 101);
			return true;
		}
		if (50 < segmentKind)
		{
			holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Dummy);
			segmentKind -= 50;
		}
		switch (segmentKind)
		{
		case 1:
		case 2:
			holdStep.flag = setFlag(holdStep.flag, HoldNoteFlag::Critical, segmentKind == 2);
			return true;
		default:
			return false;
		}
	}

	HoldStepLayer PySekaiEngine::fromLayerNumeric(int segmentLayer)
	{
		switch (segmentLayer)
		{
		case 0:
			return HoldStepLayer::Top;
		case 1:
			return HoldStepLayer::Bottom;
		default:
			return HoldStepLayer::LayerCount;
		}
	}
#pragma endregion

}
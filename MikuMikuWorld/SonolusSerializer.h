#pragma once
#include "ScoreSerializer.h"
#include "Sonolus.h"
#include "Constants.h"
#include <string>
#include <memory>

namespace MikuMikuWorld
{
	class SonolusEngine; // Forward declaration
	class SonolusSerializer : public ScoreSerializer
	{
	  public:
		void serialize(const SerializingScore& data, std::string filename) override;
		SerializingScore deserialize(std::string filename) override;

		SonolusSerializer(std::unique_ptr<SonolusEngine>&& engine, bool compressData = true,
		                  bool prettyDump = false)
		    : engine(std::move(engine)), compressData(compressData), prettyDump(prettyDump)
		{
		}

	  private:
		std::unique_ptr<SonolusEngine> engine;
		bool compressData;
		bool prettyDump;
	};

	class SonolusEngine
	{
	  protected:
		using IntegerType = Sonolus::LevelDataEntity::IntegerType;
		using RealType = Sonolus::LevelDataEntity::RealType;
		using RefType = Sonolus::LevelDataEntity::RefType;
		using FuncRef = std::function<RefType(void)>;
		using TickType = decltype(Note::tick);
		using WidthType = decltype(Note::width);
		using LaneType = decltype(Note::lane);

		static double toBgmOffset(float musicOffset);
		static Sonolus::LevelDataEntity toBpmChangeEntity(const Tempo& tempo);
		static RealType ticksToBeats(TickType ticks, TickType beatTicks = TICKS_PER_QUARTER);
		static RealType widthToSize(WidthType width);
		static RealType toSonolusLane(LaneType lane, WidthType width);

		static float fromBgmOffset(double bgmOffset);
		static bool fromBpmChangeEntity(const Sonolus::LevelDataEntity& bpmChangeEntity,
		                                Tempo& tempo);
		static TickType beatsToTicks(RealType beats, TickType beatTicks = TICKS_PER_QUARTER);
		static WidthType sizeToWidth(RealType size);
		static LaneType toNativeLane(RealType lane, RealType size);

	  public:
		virtual Sonolus::LevelData serialize(const SerializingScore& score) = 0;
		virtual SerializingScore deserialize(const Sonolus::LevelData& levelData) = 0;

		// virtual ~SonolusEngine() = default;
	};

	class PySekaiEngine : public SonolusEngine
	{
	  public:
		Sonolus::LevelData serialize(const SerializingScore& data) override;
		SerializingScore deserialize(const Sonolus::LevelData& levelData) override;

		static Result canSerialize(const SerializingScore& score);

	  private:
		static Sonolus::LevelDataEntity toGroupEntity(const Layer& layer);
		static Sonolus::LevelDataEntity toTimeScaleEntity(const HiSpeed& hispeed,
		                                                  const RefType& groupName);
		static Sonolus::LevelDataEntity toSkillEntity(const Skill& skill);
		static std::pair<Sonolus::LevelDataEntity, Sonolus::LevelDataEntity>
		toFeverEntities(const Fever& fever);
		static Sonolus::LevelDataEntity toNoteEntity(const Note& note, const std::string& archetype,
		                                             const RefType& groupName,
		                                             const HoldNote* hold = nullptr,
		                                             const HoldNoteStep* holdStep = nullptr);
		static std::string getSingleNoteArchetype(const Note& note);
		static std::string getHoldNoteArchetype(const Note& note, bool isHead, bool isTail);
		static void insertTransientTickNote(size_t headIndex, size_t tailIndex, bool isActiveHead,
		                                    std::vector<Sonolus::LevelDataEntity>& entities);
		static void estimateAttachEntity(Sonolus::LevelDataEntity& attach,
		                                 const Sonolus::LevelDataEntity& head,
		                                 const Sonolus::LevelDataEntity& tail);
		static int toDirectionNumeric(FlickType flick);
		static int toEaseNumeric(EaseType ease);
		static int toEffectNumeric(SoundEffectType effect);
		static int toSegmentNumeric(const HoldNoteStep* holdStep = nullptr);
		static int toLayerNumeric(const HoldStepLayer& layer);

		static bool fromGroupEntity(const Sonolus::LevelDataEntity& groupEntity, Layer& layer);
		static bool fromTimeScaleEntity(const Sonolus::LevelDataEntity& timescaleEntity,
		                                const Sonolus::LevelDataEntity& groupEntity,
		                                HiSpeed& hispeed,
		                                std::unordered_map<RefType, size_t>& groupNameMap);
		static bool fromSkillEntity(const Sonolus::LevelDataEntity& skillEntity, Skill& skill);
		static bool fromFeverEntity(const Sonolus::LevelDataEntity& feverEntity, Fever& fever);

		static int fromNoteEntity(const Sonolus::LevelDataEntity& noteEntity, Note& note,
		                          const std::unordered_map<RefType, size_t>& groupNameMap);
		static int fromTapNoteEntity(const Sonolus::LevelDataEntity& tapNoteEntity, Note& note,
		                             const std::unordered_map<RefType, size_t>& groupNameMap);

		static int fromHoldMidEntity(const Sonolus::LevelDataEntity& noteEntity, Note& note,
		                             const std::unordered_map<RefType, size_t>& groupNameMap);

		static bool isValidNoteState(const Note& note);
		static bool isValidHoldNotes(const std::vector<Note>& holdNotes, const HoldNote& hold);

		static FlickType fromDirectionNumeric(int direction);
		static EaseType fromEaseNumeric(int ease);
		static SoundEffectType fromEffectNumeric(int effectKind);
		static bool fromSegmentNumeric(int segmentKind, HoldNoteStep& holdStep);
		static HoldStepLayer fromLayerNumeric(int segmentLayer);
	};
}
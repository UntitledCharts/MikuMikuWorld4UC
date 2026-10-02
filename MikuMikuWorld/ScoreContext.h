#pragma once
#include "Audio/AudioManager.h"
#include "Audio/Waveform.h"
#include "Constants.h"
#include "HistoryManager.h"
#include "Jacket.h"
#include "JsonIO.h"
#include "Score.h"
#include "ScoreStats.h"
#include <unordered_set>
#include <atomic>
#include <optional>

namespace MikuMikuWorld
{
	enum class TimeDivisionType : uint8_t
	{
		Quarter,
		Measure,
		DivisionTypeCount
	};
	enum class SnapMode : uint8_t
	{
		// Snap notes to their own division
		Relative,
		// Snap the current notes to the nearest division
		// move other notes with the offset
		Absolute,
		// Snap each notes to their nearest division
		IndividualAbsolute,
		SnapModeMax
	};
	enum class InsertMode : uint8_t
	{
		Select,
		InsertTap,
		InsertLong,
		InsertLongMid,
		InsertFlick,
		MakeCritical,
		MakeFriction,
		InsertGuide,
		InsertDamage,
		MakeDummy,
		InsertBPM,
		InsertTimeSign,
		InsertHiSpeed,
		InsertModeMax
	};

	struct EditArgs
	{
		float noteWidth{ 3 };
		float noteAlign{ 0.5f };
		FlickType flickType{ FlickType::Default };
		EaseType easeType{ EaseType::Linear };
		SoundEffectType soundEffect{ SoundEffectType::Default };
		EditHoldJointType startType{ EditHoldJointType::Normal };
		EditHoldStepType stepType{ EditHoldStepType::Normal };
		EditHoldJointType endType{ EditHoldJointType::Normal };
		GuideColor colorType{ GuideColor::Green };
		FadeType fadeType{ FadeType::Out };
		HoldStepLayer holdLayer{ HoldStepLayer::Top };
		InsertMode insertMode{ InsertMode::Select };

		float bpm{ 160.0f };
		int timeSignatureNumerator{ 4 };
		int timeSignatureDenominator{ 4 };
		float hiSpeed{ 1.0f };
		float hiSpeedSkip{ 0.0f };
		HiSpeedEaseType hiSpeedEase{ HiSpeedEaseType::None };
		bool hiSpeedHideNotes{ false };

		bool drawHoldStepOutlines{ true };

		void changeInsertMode(InsertMode newMode);
		bool isNoteInsertMode() const;
	};

	struct PasteData : public NotesContext
	{
		NoteOrderedCollection notesOrderedView;
		HiSpeedCollection hiSpeedChanges;
		bool pasting{ false };

		float width{};
		float minLane{};

		void cancelPaste();
		void startPaste();
		void load(const nlohmann::json& data);
		void updatePasteSize();
		void flip();
	};

	enum class SelectionFlag : uint16_t
	{
		None,
		// Has any notes that can be set trace
		CanTrace = 1 << 0,
		// Has any notes that can be set critical
		CanCritical = 1 << 1,
		// Has any notes that have Flick Type
		CanFlick = 1 << 2,
		// Has any notes that can be set dummy
		CanDummy = 1 << 3,
		// Has any notes inside a active hold note
		HasHoldNote = 1 << 4,
		// Has any notes inside a guide note
		HasGuideNote = 1 << 5,
		// Has any notes between the hold start and end notes
		HasAnyHoldMid = 1 << 6,
		// Has any notes where HoldNoteStep.ID == note.ID
		HasAnyHoldNoteStep = 1 << 7,
		// Has any hold start + hold mid not attached
		CanEase = 1 << 8,
		// Has any notes that have guide alpha property enabled
		HasGuideAlphaNote = 1 << 9,
		// Has any notes that can set guide alpha property
		CanSetGuideAlphaNote = 1 << 10,
		CanConnectHold = 1 << 11,
		CanSoundEffect = 1 << 12,
		// Flag for properties window
		DirtyProperty = 1 << 15,

	};

	struct ScoreContext
	{
		static constexpr id_t LAYER_ALL = -1;

		Score score;
		ScoreMetadata metadata;
		ScoreMetadata workingMetadata;
		std::string filename;
		ScoreStats scoreStats;
		HistoryManager history;
		std::unique_ptr<Audio::AudioContext> audio;
		Audio::WaveformMipChain waveformL, waveformR;

		bool upToDate{ true };
		bool showAllLayers = false;
		SelectionFlag selectedFlag = SelectionFlag::None;

		// pending music changes will be handled in the timeline
		bool isPendingLoadMusic{ false };
		bool isPendingChangeMusicOffset{ false };
		std::unique_ptr<std::atomic_bool> isMusicLoading =
		    std::make_unique<std::atomic_bool>(false);

		int recentHistoryUndo = 0;
		id_t nextNoteID = 0;
		id_t nextHoldID = 0;

		id_t selectedLayer = 0;
		ScoreSelection selection;
		NoteViewCollection selectedNotes; // fast lookup for selection.notes
		std::vector<Note*> hoveringNotes;
		NoteOrderedCollection notesOrderedView; // fast lookup
		WaypointOrderedCollection waypointOrderedView;

		float minNoteWidth() const noexcept;
		float maxNoteWidth() const noexcept;
		float maxNoteWidth(float lane) const noexcept;
		float minLane() const noexcept;
		float maxLane() const noexcept;
		float maxLane(float width) const noexcept;
		EaseType maxEase() const noexcept;
		FlickType maxFlick() const noexcept;

		void setStep(EditHoldStepType step);
		void setFlick(FlickType flick);
		void setEase(EaseType ease);
		void setSoundEffect(SoundEffectType sound);
		void setFadeType(FadeType fade);
		void setGuideColor(GuideColor color);
		void setLayer(int layer);
		void setCriticals(int critical = true);
		void setCriticalHold(int critical = true);
		void setFriction(int friction = true);
		void setDummy(int dummy = true);
		void setDummyHold(int dummy = true);
		void setGuideAlpha(float alpha);
		void setHoldLayer(HoldStepLayer layer);
		void setHoldSeparator(int separator = true);

		void updateSelectionFlag();
		void updateSelectionView();
		// Clear selection and its view without updating the selection flag
		void clearNoteSelection();
		void clearSelection();

		tick_t getMinTickFromSelection() const;

		void selectNote(Note& note, bool update = true);
		void selectHiSpeed(const HiSpeed& hispeed);
		void deselectNote(const Note& note, bool update = true);
		void deselectHiSpeed(const HiSpeed& hispeed);
		void selectAll(id_t layer = LAYER_ALL);
		void deselectAll();

		void deleteSelection();
		void flipSelection();
		void cutSelection();
		void copySelection() const;
		void paste(PasteData& pasteData, float offsetLane, tick_t offsetTick, id_t holdID = -1);
		void shrinkSelection(tick_t spacing);
		void compressSelection();
		bool canMoveNoteSelection(tick_t& ticks, int quarterDivision, float& lanes,
		                          float laneDivision, SnapMode snapMode);
		void moveNoteSelection(tick_t ticks, int quarterDivision, float lanes, float laneDivision,
		                       SnapMode snapMode, bool update = true);
		void setPosNoteSelection(tick_t tick, float lanes, bool update = true);

		void connectHoldsInSelection();
		void splitHoldInSelection();
		/**
		 * @brief Convert normal holds or guide notes within selection into traces
		 * @param division Current division. Used to determine the ticks between two trace notes
		 * @param deleteOrigin Delete the original hold notes or not
		 */
		void convertHoldToTraces(int quarterDivision, bool deleteHold, bool update = true);

		void lerpHiSpeeds(int quarterDivision, EaseType ease);

		void convertHoldToGuide(GuideColor color);
		void convertGuideToHold(bool critical);
		void convertHoldToNone();

		void updateViews();
		HistoryContext historyContext();
		void undo();
		void redo();
		// Legacy: does not record history. TODO(history-migration): replace with typed overload
		void pushHistory(std::string_view description);
		void pushHistory(std::string_view description, HistoryEdit edit,
		                 std::optional<FieldChange<ScoreSelection>> selectionChange = std::nullopt);

		// Commit the current working metadata field and push it onto the history
		template <typename T>
		inline void pushWorkingMetadata(T ScoreMetadata::* field, std::string_view description,
		                                bool deferred = false);

		Note* insertNote(const Note& note, id_t holdID = -1, bool update = true);
		// Will affect any view that referencing the note, like selection
		// Please make a copy if you need to erase them while iterating
		void eraseNote(Note& note, bool update = true);
		std::tuple<HoldNote&, Note&, Note&> insertHold(const Note& startNote, const Note& endNote,
		                                               const HoldNote& hold, bool update = true);
		void eraseHold(HoldNote& hold, bool update = true);
		// Connects two holds together
		id_t connectHolds(id_t currHoldID, id_t nextHoldID, bool update = true);
		std::pair<id_t, id_t> splitHoldAt(HoldNote& hold, size_t index, bool update = true);
		void insertTempoChange(const Tempo& tempo);
		void insertTimeSignature(const TimeSignature& timeSig);
		HiSpeed& insertHispeedChange(const HiSpeed& hispeed, bool update = true);
		Waypoint& insertWaypoint(tick_t tick, const std::string& name);
		void eraseWaypoint(id_t waypointID);
		void insertSkill(tick_t tick);

		void setLaneExtension(int value, bool update = true);
		void setScoreExtension(bool isExtended);

		bool isLayerVisible(id_t layer) const;
		bool isLayerInteractive(id_t layer) const;
		bool isLayerSelected(id_t layer) const;

	  private:
		void assertNoteSelection();
		void onHistoryApplied();
	};

	template <typename T>
	inline void ScoreContext::pushWorkingMetadata(T ScoreMetadata::* field,
	                                              std::string_view description, bool deferred)
	{
		T& before = metadata.*field;
		T& after = workingMetadata.*field;
		if (before == after)
			return;
		ChangeMetadata edit{ MetadataChange<T>{ field, { before, after } } };
		if (!deferred)
			before = after;
		pushHistory(description, std::move(edit));
	}
}

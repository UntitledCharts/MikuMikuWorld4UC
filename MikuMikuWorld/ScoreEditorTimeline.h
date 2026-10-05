#pragma once
#include "Background.h"
#include "ScoreContext.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/Renderer.h"
#include <variant>
#include <future>

namespace MikuMikuWorld
{
	using EventArgs = std::variant<Tempo, HiSpeed, TimeSignature, Waypoint, Fever, Skill>;

	class ScoreEditorTimeline
	{
	  public:
		ScoreContext context;

		void update(EditArgs& edit, PasteData& pasteData, Renderer* renderer);
		void updateInBackground();

		int getQuarterDivision() const noexcept;
		void setQuarterDivision(int division);
		float getLaneDivision() const noexcept;
		void setLaneDivision(float division);
		bool isPlaying() const;
		void setPlaying(bool playing, int returnToPrev = -1);
		void stop();
		secs_t getStopTime() const;
		float getPlaybackSpeed() const noexcept;
		void setPlaybackSpeed(float speed);
		void jumpToPrevDivision();
		void jumpToNextDivision();
		void scrollToCursor(float direction = 0);
		void scrollTo(secs_t time, float pivot = -1);
		ImVec2 getZoom() const;
		void setZoomY(float newZoomY, float pivot = -1);
		void openEvent(const EventArgs& args);

		void loadMusic(const std::string& filename);
		void loadScore(Score score, ScoreMetadata metadata, const std::string& filename);
		bool saveScore(const std::string& filename);

		ScoreEditorTimeline(id_t id, Audio::AudioManager& manager);
		const char* getWindowName() const;
		void setTimelineName(std::string_view name);
		std::string_view getTimelineName() const;

		secs_t getCurrentTime() const;
		tick_t getCurrentTick() const;
		measure_t getCurrentMeasure() const;
		const Jacket& getJacket() const;
		Background& getBackground();

	  private:
		enum DrawChannel
		{
			Channel_Hold_Bottom,
			Channel_Guide_Bottom,
			Channel_Hold_Top,
			Channel_Guide_Top,
			Channel_HoldOutline,
			Channel_TapNote,
			Channel_Outline,
			Channel_Friction,
			Channel_Arrow,

			Channel_Base,
			Channel_Hover = Channel_Base * 2,
			Channel_Count = Channel_Base * 3
		};
		void drawTimeline(ImDrawList* drawList);
		void drawTapNote(ImDrawList* drawList, const Note& note, const Color& tint, int baseChannel,
		                 const NotesContext& notesContext, float laneOffset = 0,
		                 tick_t tickOffset = 0);
		void drawTickNote(ImDrawList* drawList, const Note& note, const Color& tint,
		                  int baseChannel, const NotesContext& notesContext, float laneOffset = 0,
		                  tick_t tickOffset = 0);
		void drawNoteOutline(ImDrawList* drawList, const Note& note, const Color& outline,
		                     const Color& fill, int baseChannel, float laneOffset = 0,
		                     tick_t tickOffset = 0);
		void drawNote(ImDrawList* drawList, const Note& note, const Color& tint, int baseChannel,
		              const NotesContext& notesContext, float laneOffset = 0,
		              tick_t tickOffset = 0);
		void drawHoldCurve(ImDrawList* drawList, const Note& start, const Note& end,
		                   const HoldNoteStep& holdStep, float startPercent, float endPercent,
		                   const Color& startTint, const Color& endTint, int baseChannel,
		                   float laneOffset = 0, tick_t tickOffset = 0);
		using CanNoteDrawFunc = bool (*)(const Note&, void* args);
		using GetColorFunc = Color (*)(const Note&, void* args);
		using GetChannelFunc = DrawChannel (*)(const Note&, void* args);
		void drawHoldNote(ImDrawList* drawList, const HoldNote& hold,
		                  const NotesContext& notesContext, tick_t startTick, tick_t endTick,
		                  CanNoteDrawFunc canDraw, GetColorFunc getTint, GetChannelFunc getChannel,
		                  void* args = nullptr, float laneOffset = 0, tick_t tickOffset = 0);

		void drawWaveform(ImDrawList* drawList, ImU32 waveformColor,
		                  const Audio::WaveformMipChain& waveform, float secondsPerPixel,
		                  float direction);

		using NoteTransformValidator = bool (ScoreEditorTimeline::*)(const Note&, EditArgs&);
		using NoteTransformer = void (ScoreEditorTimeline::*)(Note&, EditArgs&);
		void noteControl(const char* id, Note& note, const ImVec2& pos, const ImVec2& size,
		                 EditArgs& edit, ImGuiMouseCursor cursor, NoteTransformValidator validator,
		                 NoteTransformer transformer);
		bool canNoteResizeL(const Note& note, EditArgs& edit);
		void noteResizeL(Note& note, EditArgs& edit);
		bool canNoteResizeR(const Note& note, EditArgs& edit);
		void noteResizeR(Note& note, EditArgs& edit);
		bool canNoteMove(const Note& note, EditArgs& edit);
		void noteMove(Note& note, EditArgs& edit);
		void noteSelector();
		id_t findHoveringHoldNote();

		static bool eventControl(ImDrawList* drawList, const char* txt, ImU32 color, float x,
		                         float y, bool enabled);
		bool bpmControl(ImDrawList* drawList, const Tempo& tempo, bool enable = true);
		bool timeSignatureControl(ImDrawList* drawList, const TimeSignature& ts,
		                          bool enabled = true);
		bool hiSpeedControl(ImDrawList* drawList, const HiSpeed& hispeed, bool selected,
		                    bool enabled = true);
		bool skillControl(ImDrawList* drawList, const Skill& skill, bool enabled = true);
		bool feverControl(ImDrawList* drawList, const Fever& fever, bool enabled = true);
		bool waypointControl(ImDrawList* drawList, const Waypoint& waypoint, bool enabled = true);

		Tempo getPreviewTempo(const EditArgs& edit) const;
		TimeSignature getPreviewTimeSignatrue(const EditArgs& edit) const;
		HiSpeed getPreviewHispeed(const EditArgs& edit) const;
		void eventEditor();

		void updateTimelineOffset();
		void updateContextMenu(PasteData& pasteData);
		void updateStatusBar();
		void updateScrollBar(ImDrawList* drawList);
		void updatePlayback();
		void updateNotes(ImDrawList* drawList, EditArgs& edit, PasteData& pasteData);
		void updateNote(Note& note, EditArgs& edit);
		void updatePreviewNote(EditArgs& edit);
		void updateScoreEvents(ImDrawList* drawList, EditArgs& edit, PasteData& pasteData);
		void debug();

	  private:
		ImVec2 absScreenPos;
		ImVec2 maxScreenPos;
		ImVec2 timelineScreenPos;
		ImVec2 timelineScreenSize;
		ImVec2 timelinePos;
		ImVec2 timelineSize;
		ImVec2 leftPanelScreenPos;
		ImVec2 rightPanelScreenPos;
		ImVec2 panelScreenSize;
		ImVec2 toolbarScreenPos;

		ImVec2 targetOffset = { 0, 0 };
		ImVec2 visualOffset = { 0, 0 };
		secs_t prevTime = 0;
		secs_t curTime = 0;
		secs_t lastFrameTime = 0;
		secs_t maxTime = 18;
		float zoomX = 1.0f;
		float zoomY = 2.0f;
		float laneDivision = 1; // 1 = 12 lanes
		int quarterDivision = 2;
		tick_t mouseTick = 0;
		float mouseLane = 0;
		tick_t shiftingTick = 0;
		float shiftingLane = 0;
		secs_t dragStartTime = 0;
		float dragStartLane = 0;
		float playbackSpeed = 1;
		float noteHeight = MIN_NOTES_HEIGHT;
		float selectHoverTimer = 0.5f;
		id_t grabbingNote = -1;
		tick_t snapTick{};
		tick_t inputTick{};
		float inputLane{};
		float hispeedPanelStartX{};
		float hispeedExPanelStartX{};

		const id_t windowID;
		const std::string windowNameKey;
		std::string windowName;

		bool mouseClicked = false;
		bool mouseInTimeline = false;
		bool playing = false;
		bool snapToTargetTime = false;
		bool isInsertingNote = false;
		bool isInsertingHold = false;
		bool isInsertingEvent = false;
		bool isDragSelecting = false;
		bool isMovingNote = false;
		bool shouldOpenEventEditor = false;
		bool drawHoldStepOutlines = true;
		SnapMode snapMode = SnapMode::Relative;
		InsertMode insMode = InsertMode::Select;
		// Track undo/redo while the editor is openned
		int eventEditHistoryCursor = 0;

		NotesContext previewNotes;
		EventArgs eventEditArgs;
		Background background;
		Jacket jacket;
		ImDrawListSplitter drawSplitter;
		std::future<void> loadMusicFuture;
		std::unordered_set<std::string> playingNoteSounds;

	  public:
		constexpr static float ORIGIN_X = 6.f;
		constexpr static float ORIGIN_Y = 1.f;
		constexpr static float UNIT_X = 16.f; // = 16 lanes
		constexpr static float UNIT_Y = 5.f;  // = 5 seconds
		constexpr static float ZOOM_Y_MIN = 1 / 4.f;
		constexpr static float ZOOM_Y_MAX = 64.f;
		constexpr static float ZOOM_Y_WHEEL_MAX = 192.f;
		constexpr static float ZOOM_Y_FACTOR = 1.25f;
		constexpr static float TIMELINE_SIZE_FACTOR = 0.55f;
		constexpr static float PANEL_SIZE_MIN = 180; // px
		constexpr static float WHEEL_FACTOR = 0.25f;
		constexpr static float PAN_FACTOR = 0.05f;
		constexpr static float TIMELINE_SCREEN_Y_MIN_OFFSET = 30; // px

		constexpr static float MEASURE_X_OFFSET = 30;
		constexpr static float BEAT_X_OFFSET = 12;
		constexpr static float PLAYBACK_CURSOR_SIZE = 12;

		constexpr static int NUM_LANES = 12;
		constexpr static int MIN_LANE_WIDTH = 24;
		constexpr static int MAX_LANE_WIDTH = 72;
		constexpr static int MIN_NOTES_HEIGHT = 24;
		constexpr static int MAX_NOTES_HEIGHT = 72;

		constexpr static float NOTE_CTRL_WIDTH = 12;
		constexpr static float NOTE_SIDE_PADDING = 5;
		constexpr static float NOTE_SIDE_WIDTH = 18;
		constexpr static float HOLD_PATH_PADDING = 2;
		constexpr static float HOLD_NOTE_SIDE_WIDTH = 5;
		constexpr static float HOLD_MID_PADDING = 5;

		constexpr static int QUART_DIVISIONS[]{ 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 480 };
		constexpr static int MEASURE_DIVISIONS[]{ 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 1920 };
		constexpr static int LANE_DIVISIONS[]{ 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 96, 192 };

		constexpr static float MIN_PLAYBACK_SPEED = 0.25f;
		constexpr static float MAX_PLAYBACK_SPEED = 1.00f;

		constexpr static float AUDIO_LOOK_AHEAD = 0.05f;
		constexpr static float AUDIO_CORRECTION_OFFSET = 0.02f;

		enum StepType
		{
			Step_Normal,
			Step_Skip,
			Step_Hidden,
			Step_HiddenCritical,
			Step_GuideNeutral,
			Step_GuideRed,
			Step_GuideGreen,
			Step_GuideBlue,
			Step_GuideYellow,
			Step_GuidePurple,
			Step_GuideCyan,
			Step_GuideBlack,
			Step_Max
		};

		constexpr static const char windowUntitled[] = "Untitled";

	  private:
		ImVec2 screenCenter() const noexcept;
		float screenCenterX() const noexcept;
		float screenCenterY() const noexcept;
		// Translate position from the timeline pos to absolute screen pos and vice versa
		ImVec2 toScreen(ImVec2 pos) const noexcept;
		float toScreenPosX(float lane) const noexcept;
		float toScreenPosY(secs_t secs) const noexcept;
		ImVec2 fromScreen(ImVec2 screenPos) const noexcept;
		float toLanePos(float screenX) const noexcept;
		secs_t toTimePos(float screenY) const noexcept;
		// Translate units from the timeline to the screen
		float toScreenWidth(float lanes) const noexcept;
		float toScreenHeight(secs_t secs) const noexcept;
		float toLaneUnit(float width) const noexcept;
		secs_t toTimeUnit(float height) const noexcept;
	};
}

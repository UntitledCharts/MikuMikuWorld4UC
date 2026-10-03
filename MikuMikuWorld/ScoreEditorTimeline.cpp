#include "ScoreEditorTimeline.h"
#include "Application.h"
#include "ApplicationConfiguration.h"
#include "Colors.h"
#include "Constants.h"
#include "Tempo.h"
#include "Score.h"
#include "UI.h"
#include "Utilities.h"
#include "NativeScoreSerializer.h"
#include <algorithm>
#include <string>

namespace MikuMikuWorld
{
	static_assert(std::size(stepOutlines) == ScoreEditorTimeline::StepType::Step_Max);
	static_assert(std::size(stepFills) == ScoreEditorTimeline::StepType::Step_Max);

	ScoreEditorTimeline::ScoreEditorTimeline(id_t id, Audio::AudioManager& manager)
	    : windowID(id),
	      windowNameKey(std::string("__notes_timeline_").append(std::to_string(windowID)))
	{
		windowName = windowUntitled;
		windowName.append("###notes_timeline");
		if (windowID != 0)
			windowName.append(std::to_string(windowID));

		context.audio = std::make_unique<Audio::AudioContext>(manager);
	}

	static std::string createTitleName(const std::string& name)
	{
		return IO::formatString(ICON_FA_MUSIC " %s - %s", localize(Text::notesTimeline), name);
	}

	const char* ScoreEditorTimeline::getWindowName() const
	{
		return localizeOrInsert(windowNameKey, createTitleName, windowName);
	}

	std::string_view ScoreEditorTimeline::getTimelineName() const
	{
		std::string_view name = windowName;
		return name.substr(0, name.find("###notes_timeline"));
	}

	void ScoreEditorTimeline::setTimelineName(std::string_view name)
	{
		windowName = name;
		windowName.append("###notes_timeline");
		if (windowID != 0)
			windowName.append(std::to_string(windowID));
		getLocalization().updateText(windowNameKey, { createTitleName(windowName) });
	}

	void ScoreEditorTimeline::update(EditArgs& edit, PasteData& pasteData, Renderer* renderer)
	{
		auto& config = getConfig();
		const ImGuiIO& io = ImGui::GetIO();
		const ImGuiStyle& style = ImGui::GetStyle();

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (!drawList)
		{
			updateInBackground();
			return;
		}

		if (context.isPendingLoadMusic)
		{
			if (loadMusicFuture.valid())
				loadMusicFuture.get();
			loadMusicFuture = std::async(std::launch::async, &ScoreEditorTimeline::loadMusic, this,
			                             context.workingMetadata.musicFile);
			context.isPendingLoadMusic = false;
		}

		if (context.isMusicLoading->load() == false && context.isPendingChangeMusicOffset)
		{
			context.isPendingChangeMusicOffset = false;
			context.audio->setMusicOffset(getCurrentTime(), context.workingMetadata.musicOffset);
		}

		ImVec2 absScreenSize = ImGui::GetContentRegionAvail();
		// Make space for the scrollbar, the status bar and the side panels
		absScreenSize -= { style.ScrollbarSize, UI::toolbarBtnSize.y };
		absScreenPos = ImGui::GetCursorScreenPos();
		maxScreenPos = absScreenPos + absScreenSize;

		float laneCount = std::max(context.metadata.laneExtension * 2.f + NUM_LANES, UNIT_X);
		if (config.matchTimelineSizeToWindow)
		{
			// Scale the width to make sure x lanes is always visible
			timelineScreenSize.x =
			    std::clamp(absScreenSize.x * TIMELINE_SIZE_FACTOR, MIN_LANE_WIDTH * laneCount,
			               MAX_LANE_WIDTH * laneCount);
			zoomX = UNIT_X / laneCount;
		}
		else
		{
			timelineScreenSize.x =
			    std::max(config.timelineWidth * UNIT_X, MIN_LANE_WIDTH * laneCount);
			zoomX = (config.timelineWidth * UNIT_X) / timelineScreenSize.x;
		}
		noteHeight =
		    getConfig().matchNotesSizeToTimeline ? toScreenWidth(1) : getConfig().notesHeight;

		// Prevent weird edge cases
		timelineScreenSize.x = std::max(timelineScreenSize.x, 1.f);
		timelineScreenSize.y = std::max(absScreenSize.y, 1.f);
		timelineScreenPos.x = absScreenPos.x + (absScreenSize.x - timelineScreenSize.x) / 2;
		timelineScreenPos.y = absScreenPos.y;

		float panelSize =
		    std::max(UI::scale(PANEL_SIZE_MIN), timelineScreenPos.x - absScreenPos.x) -
		    style.ItemSpacing.x * 2;
		leftPanelScreenPos = absScreenPos;
		rightPanelScreenPos.x = maxScreenPos.x - panelSize;
		rightPanelScreenPos.y = absScreenPos.y;
		panelScreenSize = { panelSize, absScreenSize.y };
		// Centered the toolbar between the timeline and the edge of the window
		toolbarScreenPos.x = absScreenPos.x;
		toolbarScreenPos.y = maxScreenPos.y + style.WindowPadding.y / 2 + style.ItemSpacing.y / 2;

		ImGui::PushClipRect(absScreenPos, maxScreenPos, true);
		drawList->AddRectFilled(absScreenPos, maxScreenPos, 0xff202020);
		if (jacket.getFilename() != context.metadata.jacketFile)
		{
			jacket.load(context.metadata.jacketFile);
			if (background.hasJacket())
				background.setDirty();
		}
		if (background.isDirty())
			background.process(renderer, jacket);
		if (config.drawBackground)
		{
			background.setBrightness(config.backgroundBrightness);
			background.draw(drawList, absScreenPos, maxScreenPos);
		}
		mouseInTimeline = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		                  ImGui::IsMouseHoveringRect(absScreenPos, maxScreenPos);
		mouseClicked = mouseInTimeline && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		               !UI::isAnyPopupOpen();
		ImVec2 absMousePos = ImGui::GetMousePos();

		// Handle zoom + scrolling offset + start click select + dragging
		if (mouseInTimeline && (io.MouseWheel || io.MouseWheelH))
		{
			if (io.KeyCtrl)
			{
				float zoom = std::clamp(
				    zoomY * std::exp(io.MouseWheel * WHEEL_FACTOR * config.zoomSensitivity),
				    ZOOM_Y_MIN, ZOOM_Y_WHEEL_MAX);
				setZoomY(zoom, 1.0 + (timelineScreenPos.y - absMousePos.y) / timelineScreenSize.y);
			}
			else
			{
				float scrollSpeed =
				    (io.KeyShift ? getConfig().scrollSpeedShift : getConfig().scrollSpeedNormal);
				targetOffset.y += io.MouseWheel * WHEEL_FACTOR * scrollSpeed / zoomY;
				targetOffset.x += io.MouseWheelH * WHEEL_FACTOR * scrollSpeed;
			}
		}

		if (mouseInTimeline && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
			targetOffset.x -= toLaneUnit(io.MouseDelta.x);
			targetOffset.y += toTimeUnit(io.MouseDelta.y);
		}

		if (mouseInTimeline && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			pasteData.cancelPaste();
			isInsertingHold = false;
			isInsertingNote = false;
			isInsertingEvent = false;
		}

		updateTimelineOffset();

		if (edit.insertMode == InsertMode::Select && mouseClicked && !playing &&
		    !ImGui::IsAnyItemHovered())
		{
			// Clicked inside timeline, the current mouse position is the first drag point
			dragStartLane = mouseLane;
			dragStartTime = toTimePos(absMousePos.y);
			isDragSelecting = true;
		}

		// ImGui::PushClipRect(leftPanelScreenPos + ImVec2{ panelScreenSize.x, 0 },
		//                     rightPanelScreenPos + ImVec2{ 0, panelScreenSize.y }, true);
		drawTimeline(drawList);

		// Fail safe
		if (edit.insertMode != insMode)
		{
			isInsertingHold = false;
			isInsertingNote = false;
			isInsertingEvent = false;
		}
		drawHoldStepOutlines = edit.drawHoldStepOutlines;
		// Update notes
		updateNotes(drawList, edit, pasteData);

		// Draw selected boxes for notes
		for (auto&& [_, pnote] : context.selectedNotes)
		{
			const Note& note = *pnote;
			const int PADDING = 3;
			secs_t noteTime = accumulateDuration(note.tick, context.score.tempoChanges);
			float x = toScreenPosX(note.lane), y = toScreenPosY(noteTime) - noteHeight / 2;
			ImVec2 p1 = { x - PADDING, y };
			ImVec2 p2 = { x + PADDING + toScreenWidth(note.width), y + noteHeight };

			drawList->AddRectFilled(p1, p2, 0x20f4f4f4, 2.0f, ImDrawFlags_RoundCornersAll);
			drawList->AddRect(p1, p2, 0xcccccccc, 2.0f, ImDrawFlags_RoundCornersAll, 2.0f);
		}

		float hoverTime = std::min(std::max(ImGui::GetKeyData(ImGuiMod_Alt)->DownDuration,
		                                    ImGui::GetKeyData(ImGuiMod_Ctrl)->DownDuration),
		                           GImGui->MouseStationaryTimer);
		if (mouseClicked)
		{
			if (!io.KeyCtrl && !io.KeyAlt && context.hoveringNotes.empty())
				context.deselectAll();
			for (auto it = context.hoveringNotes.rbegin(), end = context.hoveringNotes.rend();
			     it != end; ++it)
			{
				Note& note = **it;
				bool isSelected = context.selection.has(note);
				if (isSelected && io.KeyAlt)
				{
					context.deselectNote(note);
					break;
				}
				else if (!isSelected)
				{
					if (!io.KeyCtrl)
					{
						context.deselectAll();
						if (grabbingNote >= 0)
							context.selectNote(context.score.notes.at(grabbingNote), true);
					}
					else
						context.selectNote(note, true);
					break;
				}
			}
			selectHoverTimer = std::max(0.5f, hoverTime);
		}
		else if (mouseInTimeline && (io.MouseWheel || io.MouseWheelH))
		{
			// Trying to scroll not selecting note so we need to increase the timer
			selectHoverTimer = std::max(0.0f, hoverTime) + 0.5f;
		}
		else if (!isDragSelecting && context.hoveringNotes.size() > 1 &&
		         !ImGui::IsPopupOpen("NoteSelector") && hoverTime > selectHoverTimer)
		{
			ImGui::OpenPopup("NoteSelector", ImGuiPopupFlags_NoReopen);
			selectHoverTimer = 0.5f;
		}
		else if (ImGui::IsKeyReleased(ImGuiMod_Alt) || ImGui::IsKeyReleased(ImGuiMod_Ctrl))
		{
			selectHoverTimer = 0.5f;
		}
		noteSelector();
		// ImGui::PopClipRect();

		// Update event controls
		updateScoreEvents(drawList, edit, pasteData);
		insMode = edit.insertMode;

		// Update cursor tick after determining whether a note is hovered
		// The cursor tick should not change if a note is hovered
		if (mouseClicked && !playing && edit.insertMode == InsertMode::Select &&
		    ImGui::IsWindowFocused() && !ImGui::IsAnyItemHovered() && !pasteData.pasting)
			prevTime = curTime = accumulateDuration(snapTick, context.score.tempoChanges);

		// Selection rectangle
		if (isDragSelecting && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
		    ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, 15.0f) && !pasteData.pasting)
		{
			// Clicked and dragging inside the timeline
			ImVec2 start = toScreen({ dragStartLane, (float)dragStartTime });
			ImVec2& end = absMousePos;
			drawList->AddRectFilled(start, end, selectionColor1);
			drawList->AddRect(start, end, 0xbbcccccc, 0.2f, ImDrawFlags_RoundCornersAll, 1.0f);
			if (io.KeyCtrl || io.KeyAlt)
			{
				ImVec2 iconPos = end - ImVec2{ 12, 12 };
				const char* icon = io.KeyCtrl ? ICON_FA_PLUS_CIRCLE : ICON_FA_MINUS_CIRCLE;
				drawList->AddText(ImGui::GetFont(), 12, iconPos, 0xdddddddd, icon);
			}

			if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)
			{
				if (!io.KeyAlt && !io.KeyCtrl)
				{
					context.clearSelection();
				}
				secs_t mouseTime = toTimePos(absMousePos.y);
				auto&& [startTime, endTime] = std::minmax(dragStartTime, mouseTime);
				auto&& [startLane, endLane] = std::minmax(dragStartLane, mouseLane);
				secs_t extend = toTimeUnit(noteHeight) / 2;
				tick_t startTick = accumulateTicks(startTime - extend, context.score.tempoChanges);
				tick_t endTick = accumulateTicks(endTime + extend, context.score.tempoChanges);
				auto noteIt = context.notesOrderedView.lower_bound(startTick);
				auto noteEnd = context.notesOrderedView.upper_bound(endTick);
				for (; noteIt != noteEnd; ++noteIt)
				{
					Note& note = *noteIt->second;
					if (!context.isLayerInteractive(note.layer) ||
					    !context.isLayerVisible(note.layer))
						continue;
					if (startLane > note.lane + note.width || endLane < note.lane)
						continue;
					if (io.KeyAlt)
						context.deselectNote(note, false);
					else
						context.selectNote(note, false);
				}
				extend = toTimeUnit(ImGui::GetFrameHeight());
				startTick = accumulateTicks(startTime - extend, context.score.tempoChanges);
				endTick = accumulateTicks(endTime, context.score.tempoChanges);
				float lowerbound = toLanePos(hispeedPanelStartX);
				float middlebound = toLanePos(hispeedExPanelStartX);
				if (startLane < middlebound && endLane > lowerbound)
				{
					const auto& hispeedChanges =
					    context.score.layers[context.selectedLayer].hiSpeedChanges;
					for (auto hscIt = hispeedChanges.lower_bound(startTick),
					          hscEnd = hispeedChanges.upper_bound(endTick);
					     hscIt != hscEnd; ++hscIt)
						context.selection.hispeeds.insert(
						    { hscIt->second.layer, hscIt->second.tick });
				}
				if (context.showAllLayers && startLane < toLanePos(maxScreenPos.x) &&
				    endLane > middlebound)
				{
					for (auto&& layer : context.score.layers)
					{
						if (layer.hidden || &layer == &context.score.layers[context.selectedLayer])
							continue;
						for (auto hscIt = layer.hiSpeedChanges.lower_bound(startTick),
						          hscEnd = layer.hiSpeedChanges.upper_bound(endTick);
						     hscIt != hscEnd; ++hscIt)
							context.selection.hispeeds.insert(
							    { hscIt->second.layer, hscIt->second.tick });
					}
				}
			}

			// Panning timeline
			if (absScreenSize.y > 50)
			{
				const float upPanThreshold = absScreenPos.y + absScreenSize.y * 0.075f;
				const float downPanThreshold = absScreenPos.y + absScreenSize.y * 0.925f;
				if (absMousePos.y < upPanThreshold)
					targetOffset.y +=
					    toTimeUnit(std::min(upPanThreshold - absMousePos.y, 200.f) / 10);
				else if (absMousePos.y > downPanThreshold)
					targetOffset.y -=
					    toTimeUnit(std::min(absMousePos.y - downPanThreshold, 200.f) / 10);
			}
			if (absScreenSize.x > 50)
			{
				const float leftPanThreshold = absScreenPos.x + absScreenSize.x * 0.05f;
				const float rightPanThreshold = absScreenPos.x + absScreenSize.x * 1.025f;
				if (absMousePos.x < leftPanThreshold)
					targetOffset.x -=
					    toLaneUnit(std::min(leftPanThreshold - absMousePos.x, 200.f) / 10);
				else if (absMousePos.x > rightPanThreshold)
					targetOffset.x +=
					    toLaneUnit(std::min(absMousePos.x - rightPanThreshold, 200.f) / 10);
			}
		}
		if (isDragSelecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			isDragSelecting = false;
			context.updateSelectionView();
			context.updateSelectionFlag();
		}

		ImGui::PopClipRect();

		updateContextMenu(pasteData);
		updateStatusBar();
		updateScrollBar(drawList);
		updatePlayback();

		if (getConfig().debugEnabled)
			this->debug();
	}

	void ScoreEditorTimeline::updateInBackground()
	{
		updateTimelineOffset();
		updatePlayback();

		if (getConfig().debugEnabled)
			debug();
	}

	int ScoreEditorTimeline::getQuarterDivision() const noexcept { return quarterDivision; }

	void ScoreEditorTimeline::setQuarterDivision(int division)
	{
		quarterDivision = std::clamp(division, QUART_DIVISIONS[0],
		                             QUART_DIVISIONS[std::size(QUART_DIVISIONS) - 1]);
	}

	float ScoreEditorTimeline::getLaneDivision() const noexcept { return laneDivision; }

	void ScoreEditorTimeline::setLaneDivision(float division)
	{
		laneDivision = std::clamp(int(division * 12), LANE_DIVISIONS[0],
		                          LANE_DIVISIONS[std::size(LANE_DIVISIONS) - 1]) /
		               12.f;
	}

	bool ScoreEditorTimeline::isPlaying() const { return playing; }

	void ScoreEditorTimeline::setPlaying(bool playing, int returnToPrev)
	{
		if (this->playing == playing)
			return;
		if (returnToPrev < 0)
			returnToPrev = getConfig().returnToLastSelectedTickOnPause;

		this->playing = playing;
		if (playing)
		{
			lastFrameTime = prevTime = curTime;
			if (context.audio->isMusicInitialized() && !context.isMusicLoading->load())
				context.audio->playMusic(curTime);
			context.audio->syncPlaybackTime(curTime);
			isInsertingNote = false;
			isInsertingHold = false;
			isInsertingEvent = false;
		}
		else
		{
			if (returnToPrev)
			{
				curTime = prevTime;
				scrollToCursor();
			}

			context.audio->stopSoundEffects(false);
			context.audio->stopMusic();
		}
	}

	void ScoreEditorTimeline::stop()
	{
		playing = false;
		curTime = prevTime = 0;
		targetOffset.y = 0;

		context.audio->stopSoundEffects(false);
		context.audio->stopMusic();
	}

	secs_t ScoreEditorTimeline::getStopTime() const
	{
		secs_t musicStop =
		    context.audio->isMusicInitialized() ? context.audio->getMusicEndTime() : 0.0f;
		measure_t stopMeasure = 1;
		if (context.notesOrderedView.size())
		{
			stopMeasure = accumulateMeasures(context.notesOrderedView.rbegin()->first,
			                                 context.score.timeSignatures);
			stopMeasure += 1;
		}
		secs_t noteStop = accumulateDuration(
		    accumulateTicks(stopMeasure, context.score.timeSignatures), context.score.tempoChanges);
		return std::max(musicStop, noteStop);
	}

	float ScoreEditorTimeline::getPlaybackSpeed() const noexcept { return playbackSpeed; }

	void ScoreEditorTimeline::setPlaybackSpeed(float speed)
	{
		playbackSpeed = std::clamp(speed, MIN_PLAYBACK_SPEED, MAX_PLAYBACK_SPEED);
		context.audio->setPlaybackSpeed(playbackSpeed, curTime);
	}

	void ScoreEditorTimeline::jumpToPrevDivision()
	{
		qnote_t quarter = ticksToQuarters(getCurrentTick());
		const qnote_t zero = 0, one = 1, epsilon = 0.5f / TICKS_PER_QUARTER;
		qnote_t quarterPerDivision = one / quarterDivision;
		qnote_t offset = std::fmod(quarter, quarterPerDivision);
		quarter = std::max(quarter - (isClose(offset, zero, epsilon) ? quarterPerDivision : offset),
		                   zero);
		prevTime = curTime;
		curTime = accumulateDuration(quartersToTicks(quarter), context.score.tempoChanges);
		scrollToCursor(-1);
	}

	void ScoreEditorTimeline::jumpToNextDivision()
	{
		qnote_t quarter = ticksToQuarters(getCurrentTick());
		const qnote_t zero = 0, one = 1, epsilon = 0.5f / TICKS_PER_QUARTER;
		qnote_t quarterPerDivision = one / quarterDivision;
		qnote_t offset = quarterPerDivision - std::fmod(quarter, quarterPerDivision);
		quarter += isClose(offset, zero, epsilon) ? quarterPerDivision : offset;
		prevTime = curTime;
		curTime = accumulateDuration(quartersToTicks(quarter), context.score.tempoChanges);
		scrollToCursor(1);
	}

	void ScoreEditorTimeline::scrollToCursor(float direction)
	{
		const float upScrollPivot = getConfig().cursorPositionThreshold;
		const float downScrollPivot = 1 - upScrollPivot;
		float pivot;
		if (direction == 0)
		{
			if (upScrollPivot >= 0.5f)
				pivot = 0.5f;
			else
			{
				float viewCenter = timelinePos.y + timelineSize.y * 0.5f;
				pivot = curTime > viewCenter ? upScrollPivot : downScrollPivot;
			}
		}
		else
			pivot = direction > 0 ? upScrollPivot : downScrollPivot;
		scrollTo(curTime, pivot);
	}

	void ScoreEditorTimeline::scrollTo(secs_t time, float pivot)
	{
		pivot = std::clamp(pivot, 0.f, 1.f);
		secs_t offset = time - (timelinePos.y + timelineSize.y * pivot);
		targetOffset.y += offset;
	}

	ImVec2 ScoreEditorTimeline::getZoom() const { return { zoomX, zoomY }; }

	void ScoreEditorTimeline::setZoomY(float newZoomY, float pivot)
	{
		pivot = std::clamp(pivot, 0.f, 1.f) - 0.5;
		newZoomY = std::clamp(newZoomY, ZOOM_Y_MIN, std::max(ZOOM_Y_MAX, ZOOM_Y_WHEEL_MAX));
		float deltaY = pivot * UNIT_Y;
		targetOffset.y += deltaY * (1 / zoomY - 1 / newZoomY);
		visualOffset.y += deltaY * (1 / zoomY - 1 / newZoomY);
		zoomY = newZoomY;
	}

	void ScoreEditorTimeline::openEvent(const EventArgs& args)
	{
		shouldOpenEventEditor = true;
		eventEditArgs = args;
	}

	void ScoreEditorTimeline::drawTimeline(ImDrawList* drawList)
	{
		ImVec2 timelineMax = timelinePos + timelineSize;
		ImVec2 timelineScreenMax = timelineScreenPos + timelineScreenSize;
		const float x1 = toScreenPosX(0);
		const float x2 = toScreenPosX(NUM_LANES);
		const float xe1 = toScreenPosX(context.minLane());
		const float xe2 = toScreenPosX(context.maxLane());

		// Draw lane background
		int laneOpacity = static_cast<int>(std::clamp(getConfig().laneOpacity, 0.f, 1.f) * 255);
		ImU32 laneCol = IM_COL32(0x1f, 0x1a, 0x1c, laneOpacity);
		drawList->AddRectFilled({ x1, timelineScreenPos.y }, { x2, timelineScreenMax.y }, laneCol);
		if (context.metadata.laneExtension)
		{
			drawList->AddRectFilled({ xe1, timelineScreenPos.y }, { x1, timelineScreenMax.y },
			                        IM_COL32(0x0f, 0x1a, 0x1c, laneOpacity));
			drawList->AddRectFilled({ x2, timelineScreenPos.y }, { xe2, timelineScreenMax.y },
			                        IM_COL32(0x0f, 0x1a, 0x1c, laneOpacity));
		}

		// Draw waveform
		if (getConfig().drawWaveform)
		{
			constexpr ImU32 waveformColorL = 0x80646464;
			constexpr ImU32 waveformColorR = 0x80585858;

			const float secondsPerPixel = timelineSize.y / timelineScreenSize.y;
			drawWaveform(drawList, waveformColorL, context.waveformL, secondsPerPixel, -1);
			drawWaveform(drawList, waveformColorR, context.waveformR, secondsPerPixel, 1);
		}

		// Draw lanes
		float laneStep = 1.f / laneDivision;
		float primeLane = laneStep * 2;
		while (primeLane < 1.f)
			primeLane *= 2;
		float minLaneEx = std::max(context.minLane(), toLanePos(absScreenPos.x));
		float maxLaneEx = std::min(context.maxLane(), toLanePos(maxScreenPos.x));
		float floorLaneEx = std::floor(minLaneEx * laneDivision) / laneDivision;
		float ceilLaneEx = std::ceil(maxLaneEx * laneDivision) / laneDivision;
		for (float l = floorLaneEx; l <= ceilLaneEx; l += laneStep)
		{
			float x = toScreenPosX(l);
			bool isPrimaryLane = isDivisibleBy(l, primeLane);
			bool isExtendedLane = (l < 0 || l > NUM_LANES);
			ImU32 color = isExtendedLane ? (isPrimaryLane ? exDivColor1 : exDivColor2)
			                             : (isPrimaryLane ? divColor1 : divColor2);
			float thickness = isPrimaryLane ? primaryLineThickness : secondaryLineThickness;
			drawList->AddLine({ x, timelineScreenPos.y }, { x, timelineScreenMax.y }, color,
			                  UI::scale(thickness));
		}

		// Drawing lines
		const tick_t startTick =
		    accumulateTicks(std::max(timelinePos.y, 0.f), context.score.tempoChanges);
		const tick_t stopTick = accumulateTicks(timelineMax.y, context.score.tempoChanges);

		// Draw division lines
		qnote_t quarterPerDivision = qnote_t(1) / quarterDivision;
		// Reduce the divisions drawn at widder zooms
		auto nextTempo = context.score.tempoChanges.upper_bound(startTick);
		// require that each division is atleast 10 pixels apart
		secs_t spacingMin = toTimeUnit(10);
		float quarterPerMinute = std::prev(nextTempo)->second.quarterPerMinute;
		secs_t spacing = quartersToSecs(quarterPerDivision, quarterPerMinute);
		if (spacingMin > spacing)
		{
			float scale = roundUpToPowerOfTwo(std::round(spacingMin / spacing));
			quarterPerDivision = scale / quarterDivision;
		}
		ImU32 color = divColor2;
		ImU32 exColor = exDivColor2;
		float thickness = secondaryLineThickness, padding = 0;
		for (int div = std::floor(ticksToQuarters(startTick) / quarterPerDivision),
		         stopDiv = std::ceil(ticksToQuarters(stopTick) / quarterPerDivision);
		     div <= stopDiv; div++)
		{
			tick_t tick = quartersToTicks(div * quarterPerDivision);
			const float y = toScreenPosY(accumulateDuration(tick, context.score.tempoChanges));
			if (context.metadata.laneExtension)
			{
				drawList->AddLine({ xe1 - padding, y }, { x1, y }, exColor, thickness);
				drawList->AddLine({ x2, y }, { xe2 + padding, y }, exColor, thickness);
			}
			drawList->AddLine({ x1 - padding, y }, { x2 + padding, y }, color, thickness);

			if (nextTempo != context.score.tempoChanges.end() && nextTempo->first <= tick)
			{
				spacing = quartersToSecs(quarterPerDivision, nextTempo->second.quarterPerMinute);
				if (spacingMin > spacing)
				{
					float scale = roundUpToPowerOfTwo(std::round(spacingMin / spacing));
					quarterPerDivision = scale / quarterDivision;
				}
				++nextTempo;
			}
		}

		// Draw measure lines
		measure_t measure = accumulateMeasures(startTick, context.score.timeSignatures);
		beat_t measureBeat = accumulateBeats(measure, context.score.timeSignatures);
		tick_t measureTick = accumulateTicks(measure, context.score.timeSignatures);

		auto nextTS = context.score.timeSignatures.upper_bound(measure);
		auto currentTS = std::prev(nextTS);
		tick_t currentTickTS = accumulateTicks(currentTS->first, context.score.timeSignatures);
		tick_t ticksPerMeasure = TICKS_PER_QUARTER * quatersPerMeasure(currentTS->second);
		tick_t ticksPerBeats = TICKS_PER_QUARTER * quartersPerBeat(currentTS->second);

		// Reduce the number of drawn text at widder zooms
		int beatStep = 1;
		int measureStep = 1;
		nextTempo = context.score.tempoChanges.upper_bound(measureTick);
		spacingMin = toTimeUnit(ImGui::GetTextLineHeight());
		quarterPerMinute = std::prev(nextTempo)->second.quarterPerMinute;
		spacing = quartersToSecs(ticksToQuarters(ticksPerBeats), quarterPerMinute);
		if (spacingMin > spacing)
			beatStep = roundUpToPowerOfTwo(std::round(spacingMin / spacing));
		spacing = quartersToSecs(ticksToQuarters(ticksPerMeasure), quarterPerMinute);
		if (spacingMin > spacing)
			measureStep = roundUpToPowerOfTwo(std::round(spacingMin / spacing));

		ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.25f);
		for (tick_t tick = startTick - (startTick - currentTickTS) % ticksPerBeats,
		            stop = stopTick + ticksPerBeats;
		     tick < stop; tick += ticksPerBeats)
		{
			tick_t tickDiff = tick - measureTick;
			int beat = beat_t(tickDiff / ticksPerBeats) + measureBeat;

			bool resetSpacing = false;
			bool isMeasureLine = tickDiff % ticksPerMeasure == 0 && measure % measureStep == 0;
			bool isBeatLine = beat % beatStep == 0;
			if (tick >= ticksPerMeasure)
			{
				measure_t offsetMeasure = tickDiff / ticksPerMeasure;
				measure += offsetMeasure;
				measureBeat += beatsPerMeasure(currentTS->second) * offsetMeasure;
				measureTick += ticksPerMeasure * offsetMeasure;
				// Check if time signature changed on current measure
				if (nextTS != context.score.timeSignatures.end() &&
				    nextTS->second.measure <= measure)
				{
					currentTS = nextTS++;
					ticksPerMeasure = TICKS_PER_QUARTER * quatersPerMeasure(currentTS->second);
					ticksPerBeats = TICKS_PER_QUARTER * quartersPerBeat(currentTS->second);
					resetSpacing = true;
				}
			}

			if (isMeasureLine || isBeatLine)
			{
				secs_t time = accumulateDuration(tick, context.score.tempoChanges);
				const float y = toScreenPosY(time);
				if (isMeasureLine)
				{
					color = measureColor;
					exColor = exMeasureColor;
					thickness = primaryLineThickness;
					padding = MEASURE_X_OFFSET;
				}
				else
				{
					color = divColor1;
					exColor = exDivColor1;
					thickness = primaryLineThickness;
					padding = BEAT_X_OFFSET;
				}

				if (context.metadata.laneExtension)
				{
					drawList->AddLine({ xe1 - padding, y }, { x1, y }, exColor, thickness);
					drawList->AddLine({ x2, y }, { xe2 + padding, y }, exColor, thickness);
				}
				drawList->AddLine({ x1 - padding, y }, { x2 + padding, y }, color, thickness);
				if (isBeatLine)
				{
					std::string beatStr = std::to_string(beat);
					ImVec2 txtSize = ImGui::CalcTextSize(beatStr.c_str());
					float x = std::min(xe2 + MEASURE_X_OFFSET - txtSize.x / 2,
					                   rightPanelScreenPos.x - txtSize.x);
					ImU32 col = ImAlphaBlendColors(0xff010101, measureTxtColor);
					drawList->AddText({ x, y }, col, beatStr.c_str());
				}
				if (isMeasureLine)
				{
					std::string measureStr = std::to_string(measure);
					ImVec2 txtSize = ImGui::CalcTextSize(measureStr.c_str());
					float x = std::max(xe1 - MEASURE_X_OFFSET,
					                   leftPanelScreenPos.x + panelScreenSize.x + txtSize.x / 2);
					ImVec2 txtPos = { x - txtSize.x / 2, y };
					ImU32 col = ImAlphaBlendColors(0xff111111, measureTxtColor);
					drawList->AddText(txtPos, col, measureStr.c_str());
				}
			}

			if (nextTempo != context.score.tempoChanges.end() && nextTempo->first <= tick)
			{
				quarterPerMinute = nextTempo->second.quarterPerMinute;
				resetSpacing = true;
				++nextTempo;
			}

			if (resetSpacing)
			{
				spacing = quartersToSecs(ticksToQuarters(ticksPerBeats), quarterPerMinute);
				if (spacingMin > spacing)
					beatStep = roundUpToPowerOfTwo(std::round(spacingMin / spacing));
				spacing = quartersToSecs(ticksToQuarters(ticksPerMeasure), quarterPerMinute);
				if (spacingMin > spacing)
					measureStep = roundUpToPowerOfTwo(std::round(spacingMin / spacing));
			}
		}
		ImGui::PopFont();

		// Update song boundaries
		if (context.audio->isMusicInitialized())
		{
			float y1 = toScreenPosY(context.metadata.musicOffset / 1000);
			float y2 = toScreenPosY(context.audio->getMusicEndTime());

			ImU32 col = 0xFFCCCCCC;
			drawList->AddTriangleFilled({ x2, y1 }, { x2 + 10, y1 }, { x2 + 10, y1 - 10 }, col);
			drawList->AddTriangleFilled({ x2, y2 }, { x2 + 10, y2 }, { x2 + 10, y2 + 10 }, col);
		}

		// Draw playback cursor
		float dx = PLAYBACK_CURSOR_SIZE, dy = PLAYBACK_CURSOR_SIZE / 2;
		float y = toScreenPosY(curTime);
		float cx1 = x1 - dx, cx2 = x1;
		drawList->AddTriangleFilled({ cx1, y - dy }, { cx1, y + dy }, { cx2, y }, cursorColor);
		drawList->AddLine({ xe1, y }, { xe2, y }, cursorColor, primaryLineThickness + UI::scale(1));
	}

	void ScoreEditorTimeline::drawTapNote(ImDrawList* drawList, const Note& note, const Color& tint,
	                                      int baseChannel, const NotesContext& notesContext,
	                                      float laneOffset, tick_t tickOffset)
	{
		NoteResources& resource = getResources().noteResources;
		const Texture* texture = resource.getTexture();
		if (!texture)
			return;
		float noteLane = note.lane + laneOffset;
		float noteWidth = note.width;
		if (note.isAttached())
		{
			const auto& hold = notesContext.holdNotes.at(note.holdID);
			auto start = hold.jointBeforeStep(note, notesContext.notes);
			auto end = hold.jointAfterStep(note, notesContext.notes);
			assert(start != nullptr && end != nullptr);
			if (start && end)
			{
				float l1 = start->lane + laneOffset, r1 = l1 + start->width;
				float l2 = end->lane + laneOffset, r2 = l2 + end->width;
				float ratio = unlerp(start->tick, end->tick, note.tick, 0.5f);
				auto easeFunc = getEaseFunction(start->ease);
				noteLane = easeFunc(l1, l2, ratio);
				noteWidth = easeFunc(r1, r2, ratio) - noteLane;
			}
		}

		const unsigned int texID = texture->getID();
		auto&& [l, c, r, t, b] = resource.getTapNoteMapping(note);

		const float height = noteHeight;
		const float width = toScreenWidth(noteWidth);
		float centerX = toScreenPosX(noteLane) + width / 2;
		float centerY =
		    toScreenPosY(accumulateDuration(note.tick + tickOffset, context.score.tempoChanges));
		constexpr float sideWidth = NOTE_SIDE_WIDTH;
		float centerWidth = std::max(width - (NOTE_SIDE_WIDTH - NOTE_SIDE_PADDING) * 2, 0.0f);
		float top = centerY - height / 2, bottom = centerY + height / 2;
		float leftX = centerX - centerWidth / 2 - sideWidth;
		float rightX = centerX + centerWidth / 2;
		ImU32 col = tint.toImU32();

		// Note is not visible horizontally
		if (leftX > maxScreenPos.x || (rightX + sideWidth) < absScreenPos.x)
			return;

		drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_TapNote);
		drawList->AddImage(texID, { leftX, top }, { leftX + sideWidth, bottom }, { l, t }, { c, b },
		                   col);
		drawList->AddImage(texID, { rightX, top }, { rightX + sideWidth, bottom }, { r, t },
		                   { r + (c - l), b }, col);
		drawList->AddImage(texID, { centerX - centerWidth / 2, top },
		                   { centerX + centerWidth / 2, bottom }, { c, t }, { r, b }, col);

		if (hasFlag(note.flag, NoteFlag::Trace))
		{
			const Sprite* sprite = resource.getFrictionSprite(note);
			if (sprite)
			{
				ImVec2 p1 = { centerX - height / 2.5f, centerY - height / 2.5f },
				       p2 = { centerX + height / 2.5f, centerY + height / 2.5f };
				auto&& [uv1, uv2] = texture->getCoords(*sprite);
				drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_Friction);
				drawList->AddImage(texID, p1, p2, uv1, uv2, col);
			}
		}

		if (note.isFlick())
		{
			const Sprite* sprite = resource.getFlickArrowSprite(note);
			if (sprite)
			{
				constexpr float flickArrowWidths[] = { 0.95f, 1.25f, 1.8f, 2.3f, 2.6f, 3.2f };
				constexpr float flickArrowHeights[] = { 1, 1.05f, 1.2f, 1.4f, 1.5f, 1.6f };
				// Notes wider than 6 lanes also use flick arrow size 6
				int index = std::min(std::max(note.width, 1.f), 6.f) - 1;
				auto&& [uv1, uv2] = texture->getCoords(*sprite);
				float arrWidth = toScreenWidth(flickArrowWidths[index]) / 2;
				float arrHeight = flickArrowHeights[index] * height / 2;
				ImRect flickBB{ { centerX - arrWidth, centerY - arrHeight },
					            { centerX + arrWidth, centerY + arrHeight } };
				flickBB.TranslateY(-arrHeight - height * 0.1f);
				if (note.flick == FlickType::Right || note.flick == FlickType::DownRight)
					// Flip arrow to point to the right
					std::swap(uv1.x, uv2.x);
				if (note.flick >= FlickType::Down)
					// Flip arrow to point to down
					std::swap(uv1.y, uv2.y);
				drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_Arrow);
				drawList->AddImage(texID, flickBB.Min, flickBB.Max, uv1, uv2, col);
			}
		}

		if (note.isDummy())
		{
			const Sprite* sprite = resource.getDummyCrossSprite();
			if (sprite)
			{
				float width = std::max(noteWidth, 1.f);
				width = (width * 0.6f - 0.35f) / width;
				float crossWidth = toScreenWidth(width);
				auto&& [uv1, uv2] = texture->getCoords(*sprite);
				ImVec2 p1{ centerX - crossWidth, centerY - crossWidth };
				ImVec2 p2{ centerX + crossWidth, centerY + crossWidth };
				drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_Outline);
				drawList->AddImage(texID, p1, p2, uv1, uv2, col);
			}
		}
	}

	void ScoreEditorTimeline::drawTickNote(ImDrawList* drawList, const Note& note,
	                                       const Color& tint, int baseChannel,
	                                       const NotesContext& notesContext, float laneOffset,
	                                       tick_t tickOffset)
	{
		auto& resource = getResources().noteResources;
		const Texture* texture = resource.getTexture();
		const Sprite* sprite = resource.getFrictionSprite(note);
		if (!texture || !sprite)
			return;

		float noteLane = note.lane + laneOffset;
		float noteWidth = note.width;
		if (note.isAttached())
		{
			// Draw an outline of the note control
			// Update the note position to where it actually is
			const auto& hold = notesContext.holdNotes.at(note.holdID);
			auto start = hold.jointBeforeStep(note, notesContext.notes);
			auto end = hold.jointAfterStep(note, notesContext.notes);
			assert(start != nullptr && end != nullptr);
			if (start && end)
			{
				float l1 = start->lane + laneOffset, r1 = l1 + start->width;
				float l2 = end->lane + laneOffset, r2 = l2 + end->width;
				float ratio = unlerp(start->tick, end->tick, note.tick, 0.5f);
				auto easeFunc = getEaseFunction(start->ease);
				noteLane = easeFunc(l1, l2, ratio);
				noteWidth = easeFunc(r1, r2, ratio) - noteLane;
			}
		}

		const float size = (noteHeight - HOLD_MID_PADDING) / 2;
		float centerX = toScreenPosX(noteLane) + toScreenWidth(noteWidth) / 2;
		float centerY =
		    toScreenPosY(accumulateDuration(note.tick + tickOffset, context.score.tempoChanges));

		// Note is not visible horizontally
		if ((centerX - size) > maxScreenPos.x || (centerX - size) < absScreenPos.x)
			return;

		auto&& [uv1, uv2] = texture->getCoords(*sprite);
		drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_Friction);
		drawList->AddImage(texture->getID(), { centerX - size, centerY - size },
		                   { centerX + size, centerY + size }, uv1, uv2, tint.toImU32());

		if (note.isDummy())
		{
			const Sprite* sprite = resource.getDummyCrossSprite();
			if (sprite)
			{
				float width = std::min(std::max(noteWidth, 1.f), 5.f) / 4 + 0.25f;
				float crossWidth = toScreenWidth(width) / 2;
				auto&& [uv1, uv2] = texture->getCoords(*sprite);
				ImVec2 p1{ centerX - crossWidth, centerY - crossWidth };
				ImVec2 p2{ centerX + crossWidth, centerY + crossWidth };
				drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_Outline);
				drawList->AddImage(texture->getID(), p1, p2, uv1, uv2, tint.toImU32());
			}
		}
	}

	void ScoreEditorTimeline::drawNoteOutline(ImDrawList* drawList, const Note& note,
	                                          const Color& outline, const Color& fill,
	                                          int baseChannel, float laneOffset, tick_t tickOffset)
	{
		if (!drawHoldStepOutlines || (playing && getConfig().hideStepOutlinesInPlayback))
			return;
		float width = toScreenWidth(note.width);
		float left = toScreenPosX(note.lane + laneOffset);
		float centerY =
		    toScreenPosY(accumulateDuration(note.tick + tickOffset, context.score.tempoChanges));
		float height = noteHeight * 0.15f;
		ImVec2 p1 = { left, centerY - height }, p2 = { left + width, centerY + height };

		// Note is not visible horizontally
		if (p1.x > maxScreenPos.x || p2.x < absScreenPos.x)
			return;

		drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_Outline);
		drawList->AddRectFilled(p1, p2, fill.toImU32());
		drawList->AddRect(p1, p2, outline.toImU32(), 2, ImDrawFlags_RoundCornersAll, 2.5f);
	}

	void ScoreEditorTimeline::drawNote(ImDrawList* drawList, const Note& note, const Color& tint,
	                                   int baseChannel, const NotesContext& notesContext,
	                                   float laneOffset, tick_t tickOffset)
	{
		int outline, fill;
		const HoldNoteStep* step = nullptr;
		if (note.isHold())
		{
			auto it = notesContext.holdNotes.find(note.holdID);
			// No guarantee that &note == &stepNote so we have to search it
			auto& stepNote = notesContext.notes.at(note.ID);
			step = it == notesContext.holdNotes.end()
			           ? nullptr
			           : &it->second.holdStepAt(stepNote, notesContext.notes);
		}
		if (!note.isHidden())
		{
			switch (note.type)
			{
			case NoteType::Tap:
			case NoteType::Damage:
				drawTapNote(drawList, note, tint, baseChannel, notesContext, laneOffset,
				            tickOffset);
				if (note.isAttached())
					drawNoteOutline(drawList, note, stepOutlines[Step_Skip] * tint,
					                stepFills[Step_Skip] * tint, baseChannel, laneOffset,
					                tickOffset);

				break;
			case NoteType::Tick:
				drawTickNote(drawList, note, tint, baseChannel, notesContext, laneOffset,
				             tickOffset);
				outline = hasFlag(note.flag, NoteFlag::Attached) ? Step_Skip : Step_Normal;
				fill = hasFlag(note.flag, NoteFlag::LongNote)
				           ? step && step->isCrit() ? Step_HiddenCritical : Step_Hidden
				           : outline;
				drawNoteOutline(drawList, note, stepOutlines[outline] * tint,
				                stepFills[fill] * tint, baseChannel, laneOffset, tickOffset);
				break;
			}
		}
		else
		{
			if (!step)
				outline = fill = hasFlag(note.flag, NoteFlag::Attached) ? Step_Skip : Step_Normal;
			else if (step->isGuide())
			{
				outline = Step_GuideNeutral + static_cast<int>(step->guideColor);
				fill = hasFlag(note.flag, NoteFlag::LongNote) ? outline : Step_Normal;
			}
			else
			{
				outline = hasFlag(note.flag, NoteFlag::Attached) ? Step_Skip : Step_Normal;
				fill = hasFlag(note.flag, NoteFlag::LongNote)
				           ? step->isCrit() ? Step_HiddenCritical : Step_Hidden
				           : outline;
			}
			drawNoteOutline(drawList, note, stepOutlines[outline] * tint, stepFills[fill] * tint,
			                baseChannel, laneOffset, tickOffset);
		}
	}

	void ScoreEditorTimeline::drawHoldCurve(ImDrawList* drawList, const Note& start,
	                                        const Note& end, const HoldNoteStep& holdStep,
	                                        float startPercent, float endPercent,
	                                        const Color& startTint, const Color& endTint,
	                                        int baseChannel, float laneOffset, tick_t tickOffset)
	{
		NoteResources& resource = getResources().noteResources;
		const Texture* texture = resource.getTexture();
		if (!texture)
			return;
		const unsigned int texID = texture->getID();
		auto&& [l, c, r, t, b] = resource.getHoldStepMapping(holdStep);
		int channel = baseChannel;
		switch (holdStep.layer)
		{
		case HoldStepLayer::Top:
			channel += (!holdStep.isGuide() ? Channel_Hold_Top : Channel_Guide_Top);
			break;
		case HoldStepLayer::Bottom:
			channel += (!holdStep.isGuide() ? Channel_Hold_Bottom : Channel_Guide_Bottom);
			break;
		default:
			assert(false && "Unknown layer type");
		}

		secs_t startTime = accumulateDuration(start.tick + tickOffset, context.score.tempoChanges);
		float startX1 = toScreenPosX(start.lane + laneOffset);
		float startX2 = toScreenPosX(start.lane + start.width + laneOffset);
		if ((startX2 - startX1) < HOLD_PATH_PADDING * 2)
		{
			float startCenter = startX1 + (startX2 - startX1) / 2;
			startX1 = startCenter - HOLD_PATH_PADDING;
			startX2 = startCenter + HOLD_PATH_PADDING;
		}
		float startY = toScreenPosY(startTime);

		secs_t endTime = accumulateDuration(end.tick + tickOffset, context.score.tempoChanges);
		float endX1 = toScreenPosX(end.lane + laneOffset);
		float endX2 = toScreenPosX(end.lane + end.width + laneOffset);
		if ((endX2 - endX1) < HOLD_PATH_PADDING * 2)
		{
			float startCenter = endX1 + (endX2 - endX1) / 2;
			endX1 = startCenter - HOLD_PATH_PADDING;
			endX2 = startCenter + HOLD_PATH_PADDING;
		}
		float endY = toScreenPosY(endTime);

		// Curve is not visible horizontally
		if ((startX1 > maxScreenPos.x && endX1 > maxScreenPos.x) ||
		    (startX2 < absScreenPos.x && endX2 < absScreenPos.x))
			return;

		ImU32 startCol = startTint.toImU32(), endCol = startCol;
		bool lerpColor = startCol != endTint.toImU32();
		Color dumStartTint = dummyOutline.scaleAlpha(startTint.a),
		      dumEndTint = dummyOutline.scaleAlpha(endTint.a);
		ImU32 dumStartCol = dumStartTint.toImU32(), dumEndCol = dumStartCol;

		EaseFunction easeFunc = getEaseFunction(start.ease);
		int steps = 1;
		if (start.ease != EaseType::EaseNone && start.ease != EaseType::Linear)
			steps = std::clamp(std::abs(endY - startY) / 12, 8.f, 25.f);

		for (int step = 0; step < steps; step++)
		{
			float percent1 = lerp(startPercent, endPercent, float(step) / steps);
			float percent2 = lerp(startPercent, endPercent, float(step + 1) / steps);

			float sx1 = easeFunc(startX1, endX1, percent1) - HOLD_PATH_PADDING;
			float sx2 = easeFunc(startX2, endX2, percent1) + HOLD_PATH_PADDING;
			float ex1 = easeFunc(startX1, endX1, percent2) - HOLD_PATH_PADDING;
			float ex2 = easeFunc(startX2, endX2, percent2) + HOLD_PATH_PADDING;
			float y1 = lerp(startY, endY, percent1);
			float y2 = lerp(startY, endY, percent2);

			// hold not in the visible range
			if (y2 > (timelineScreenPos.y + timelineScreenSize.y))
				continue;
			// hold no longer visible
			if (y1 < timelineScreenPos.y)
				break;

			if (lerpColor)
			{
				startCol = endCol;
				endCol = Color::lerp(startTint, endTint, percent2).toImU32();
			}

			drawSplitter.SetCurrentChannel(drawList, channel);
			ImVec2 lp1 = { sx1, y1 };
			ImVec2 lp2 = { sx1 + HOLD_NOTE_SIDE_WIDTH, y1 };
			ImVec2 lp3 = { ex1 + HOLD_NOTE_SIDE_WIDTH, y2 };
			ImVec2 lp4 = { ex1, y2 };
			ImGui::AddImageQuadMultiColor(drawList, texID, lp1, lp2, lp3, lp4, { l, t }, { c, t },
			                              { c, b }, { l, b }, startCol, startCol, endCol, endCol);

			ImVec2 rp1 = { sx2 - HOLD_NOTE_SIDE_WIDTH, y1 };
			ImVec2 rp2 = { sx2, y1 };
			ImVec2 rp3 = { ex2, y2 };
			ImVec2 rp4 = { ex2 - HOLD_NOTE_SIDE_WIDTH, y2 };
			float rmx = r + (c - l);
			ImGui::AddImageQuadMultiColor(drawList, texID, rp1, rp2, rp3, rp4, { r, t }, { rmx, t },
			                              { rmx, b }, { r, b }, startCol, startCol, endCol, endCol);

			ImGui::AddImageQuadMultiColor(drawList, texID, lp2, rp1, rp4, lp3, { c, t }, { r, t },
			                              { r, b }, { c, b }, startCol, startCol, endCol, endCol);

			if (holdStep.isDummy())
			{
				if (lerpColor)
				{
					dumStartCol = dumEndCol;
					dumEndCol = Color::lerp(dumStartTint, dumEndTint, percent2).toImU32();
				}

				drawSplitter.SetCurrentChannel(drawList, baseChannel + Channel_HoldOutline);
				ImGui::AddQuadMultiColor(drawList, { sx1, y1 }, { sx1 + 4, y1 }, { ex1 + 4, y2 },
				                         { ex1, y2 }, dumStartCol, dumStartCol, dumEndCol,
				                         dumEndCol);
				ImGui::AddQuadMultiColor(drawList, { sx2 - 4, y1 }, { sx2, y1 }, { ex2, y2 },
				                         { ex2 - 4, y2 }, dumStartCol, dumStartCol, dumEndCol,
				                         dumEndCol);
			}
		}
	}

	void ScoreEditorTimeline::drawHoldNote(ImDrawList* drawList, const HoldNote& hold,
	                                       const NotesContext& notesContext, tick_t startTick,
	                                       tick_t endTick, CanNoteDrawFunc canDraw,
	                                       GetColorFunc getTint, GetChannelFunc getChannel,
	                                       void* args, float laneOffset, tick_t tickOffset)
	{
		startTick -= tickOffset;
		endTick -= tickOffset;
		if (notesContext.notes.at(hold.steps.front()).tick > endTick ||
		    notesContext.notes.at(hold.steps.back()).tick < startTick)
			return;

		const auto compare = [&notes = notesContext.notes](tick_t tick, id_t noteID)
		{ return tick < notes.at(noteID).tick; };

		auto stepBegIt = hold.steps.begin(), stepEndIt = hold.steps.end();
		// Find the first step > start tick
		auto firstStep = std::upper_bound(stepBegIt, stepEndIt, startTick, compare);
		// Go back for the last step <= start tick
		if (firstStep != stepBegIt)
			--firstStep;
		// Find the first step > end tick
		auto lastStep = std::upper_bound(stepBegIt, stepEndIt, endTick, compare);
		if (lastStep == stepEndIt)
			--lastStep;

		auto jntBegIt = hold.joints.begin(), jntEndIt = hold.joints.end();
		const Note& firstStepNote = notesContext.notes.at(*firstStep);
		const Note& lastStepNote = notesContext.notes.at(*lastStep);
		// Find the first joint >= first step
		auto stepCmp = HoldNote::HoldStepComparer(notesContext.notes);
		auto itJoint = std::lower_bound(jntBegIt, jntEndIt, firstStepNote, stepCmp);
		if (itJoint == jntEndIt || (stepCmp(firstStepNote, *itJoint) && itJoint != jntBegIt))
			--itJoint;
		// Find the first joint >= last step
		auto endJoint = std::upper_bound(itJoint, jntEndIt, lastStepNote, stepCmp);
		if (endJoint == jntEndIt)
			--endJoint;

		auto nextStepIt =
		    std::upper_bound(hold.separators.begin(), hold.separators.end(), *itJoint, stepCmp);
		const HoldNoteStep* holdStep = &*std::prev(nextStepIt);
		for (auto nextJoint = std::next(itJoint); itJoint != endJoint && nextJoint != jntEndIt;
		     itJoint = nextJoint++)
		{
			const Note& start = notesContext.notes.at(*itJoint);
			Color startTint = getTint(start, args);
			const Note& end = notesContext.notes.at(*nextJoint);
			Color endTint = getTint(end, args);

			if (nextStepIt != hold.separators.end() && stepCmp(*nextStepIt, *nextJoint))
			{
				if (*itJoint != nextStepIt->ID)
				{
					const Note& mid = notesContext.notes.at(nextStepIt->ID);
					if (canDraw(start, args))
					{
						Color midTint = getTint(mid, args);
						if (holdStep->isGuide())
						{
							startTint.a *= start.guideAlpha;
							midTint.a *= mid.guideAlpha;
						}
						float midPercent = unlerp(start.tick, end.tick, mid.tick, 1);
						drawHoldCurve(drawList, start, end, *holdStep, 0, midPercent, startTint,
						              midTint, getChannel(start, args), laneOffset, tickOffset);
					}
					holdStep = &*nextStepIt;
					++nextStepIt;
					if (canDraw(mid, args))
					{
						Color midTint = getTint(mid, args);
						if (holdStep->isGuide())
						{
							midTint.a *= mid.guideAlpha;
							endTint.a *= end.guideAlpha;
						}
						float midPercent = unlerp(start.tick, end.tick, mid.tick, 0);
						drawHoldCurve(drawList, start, end, *holdStep, midPercent, 1, midTint,
						              endTint, getChannel(mid, args), laneOffset, tickOffset);
					}
					continue;
				}
				holdStep = &*nextStepIt;
				++nextStepIt;
			}
			if (!canDraw(start, args))
				continue;
			if (holdStep->isGuide())
			{
				startTint.a *= start.guideAlpha;
				endTint.a *= end.guideAlpha;
			}
			drawHoldCurve(drawList, start, end, *holdStep, 0, 1, startTint, endTint,
			              getChannel(start, args), laneOffset, tickOffset);
		}
	}

	void ScoreEditorTimeline::drawWaveform(ImDrawList* drawList, ImU32 waveformColor,
	                                       const Audio::WaveformMipChain& waveform,
	                                       float secondsPerPixel, float direction)
	{
		if (waveform.isEmpty() || context.isMusicLoading->load())
			return;
		const float timelineCenter = toScreenPosX(6);
		const double musicOffsetInSeconds = context.metadata.musicOffset / 1000.0f;
		const Audio::WaveformMip& mip = waveform.findClosestMip(secondsPerPixel);
		const float timelineMaxTime = timelinePos.y + timelineSize.y;
		int timelineY = timelineScreenPos.y + timelineScreenSize.y;
		// Greatly reduce the amount of lines drawn while not hurting accuracy that much
		float pixelHeight = 2;
		secondsPerPixel *= 2;

		for (float t = roundToStep(timelinePos.y, 1 / secondsPerPixel); t < timelineMaxTime;
		     t += secondsPerPixel, timelineY -= pixelHeight)
		{
			// Small accuracy loss by converting to ticks but shouldn't be too noticeable
			const double secondsAtPixel = t - musicOffsetInSeconds;
			const bool outOfBounds =
			    secondsAtPixel < 0 || secondsAtPixel > waveform.durationInSeconds;

			if (outOfBounds)
				continue;
			float barValue =
			    waveform.getAmplitudeAt(mip, secondsAtPixel, secondsPerPixel) * toScreenWidth(5);

			ImVec2 p1(timelineCenter, timelineY);
			ImVec2 p2(timelineCenter + std::max(1.f, barValue) * direction, timelineY);
			drawList->AddLine(p1, p2, waveformColor, pixelHeight);
		}
	}

	void ScoreEditorTimeline::noteControl(const char* id, Note& note, const ImVec2& pos,
	                                      const ImVec2& size, EditArgs& edit,
	                                      ImGuiMouseCursor cursor, NoteTransformValidator validator,
	                                      NoteTransformer transformer)
	{
		// Do not allow editing notes during playback
		if (playing)
			return;

		ImGui::SetCursorScreenPos(pos);
		ImGui::InvisibleButton(id, size, ImGuiButtonFlags_AllowOverlap);
		if (mouseInTimeline && ImGui::IsItemHovered() && !isDragSelecting)
			ImGui::SetMouseCursor(cursor);

		// Note clicked
		if (ImGui::IsItemActivated())
		{
			grabbingNote = note.ID;
			inputTick = mouseTick;
			inputLane = toLanePos(ImGui::GetIO().MousePos.x);
		}

		// Holding note
		if (ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(cursor);
			grabbingNote = note.ID;
			if ((this->*validator)(note, edit))
				(this->*transformer)(note, edit);
		}

		// Note released
		if (ImGui::IsItemDeactivated())
		{
			grabbingNote = -1;
			if (context.pushNotesEdit("Note update"))
				context.updateSelectionFlag();
		}
	}

	bool ScoreEditorTimeline::canNoteResizeL(const Note& note, EditArgs&)
	{
		const float laneMin = context.minLane();
		const float widthMin = context.minNoteWidth(), widthMax = context.maxNoteWidth();
		switch (snapMode)
		{
		default:
		case SnapMode::Relative:
			shiftingLane = roundToStep(mouseLane - inputLane, laneDivision);
			if (shiftingLane == 0)
				return false;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float lane = pnote->lane + shiftingLane;
				float width = pnote->width - shiftingLane;
				if (!isWithinRange(width, widthMin, widthMax) ||
				    !isWithinRange(lane, laneMin, context.maxLane(width)))
					return false;
			}
			return true;
		case SnapMode::Absolute:
			shiftingLane = roundToStep(mouseLane, laneDivision) - note.lane;
			if (shiftingLane == 0 || std::abs(mouseLane - inputLane) == 0)
				return false;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float lane = pnote->lane + shiftingLane;
				float width = pnote->width - shiftingLane;
				if (!isWithinRange(width, widthMin, widthMax) ||
				    !isWithinRange(lane, laneMin, context.maxLane(width)))
					return false;
			}
			return true;
		case SnapMode::IndividualAbsolute:
		{
			shiftingLane = roundToStep(mouseLane, laneDivision) - note.lane;
			if (shiftingLane == 0 || std::abs(mouseLane - inputLane) == 0)
				return false;
			auto snapFn = shiftingLane > 0 ? ceilf : floorf;
			float offset = snapFn(shiftingLane * laneDivision) / laneDivision;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float lane = snapFn(pnote->lane * laneDivision) / laneDivision;
				if (pnote->lane == lane)
					lane += offset;
				float width = pnote->width - (lane - pnote->lane);
				if (!isWithinRange(width, widthMin, widthMax) ||
				    !isWithinRange(lane, laneMin, context.maxLane(width)))
					return false;
			}
			return true;
		}
		}
	}

	void ScoreEditorTimeline::noteResizeL(Note& note, EditArgs&)
	{
		switch (snapMode)
		{
		default:
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				pnote->width -= shiftingLane;
				pnote->lane += shiftingLane;
			}
			inputLane = note.lane;
			break;
		case SnapMode::Absolute:
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				pnote->width -= shiftingLane;
				pnote->lane += shiftingLane;
			}
			inputLane = mouseLane;
			break;
		case SnapMode::IndividualAbsolute:
		{
			auto snapFn = shiftingLane > 0 ? ceilf : floorf;
			float offset = snapFn(shiftingLane * laneDivision) / laneDivision;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				Note& n = *pnote;
				float lane = snapFn(n.lane * laneDivision) / laneDivision;
				if (n.lane == lane)
				{
					n.width -= offset;
					n.lane += offset;
				}
				else
				{
					n.width = n.width - (lane - n.lane);
					n.lane = lane;
				}
			}
			inputLane = mouseLane;
			break;
		}
		}
	}

	bool ScoreEditorTimeline::canNoteResizeR(const Note& note, EditArgs&)
	{
		const float widthMin = context.minNoteWidth();
		switch (snapMode)
		{
		default:
		case SnapMode::Relative:
			shiftingLane = roundToStep(mouseLane - inputLane, laneDivision);
			if (shiftingLane == 0)
				return false;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float width = pnote->width + shiftingLane;
				if (!isWithinRange(width, widthMin, context.maxNoteWidth(pnote->lane)))
					return false;
			}
			return true;
		case SnapMode::Absolute:
			shiftingLane = roundToStep(mouseLane - note.lane, laneDivision) - note.width;
			if (shiftingLane == 0 || std::abs(mouseLane - inputLane) == 0)
				return false;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float width = pnote->width + shiftingLane;
				if (!isWithinRange(width, widthMin, context.maxNoteWidth(pnote->lane)))
					return false;
			}
			return true;
		case SnapMode::IndividualAbsolute:
		{
			shiftingLane = roundToStep(mouseLane - note.lane, laneDivision) - note.width;
			if (shiftingLane == 0 || std::abs(mouseLane - inputLane) == 0)
				return false;
			auto snapFn = shiftingLane > 0 ? ceilf : floorf;
			float offset = snapFn(shiftingLane * laneDivision) / laneDivision;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float width = snapFn(pnote->width * laneDivision) / laneDivision;
				if (pnote->width == width)
					width += offset;
				if (!isWithinRange(width, widthMin, context.maxNoteWidth(pnote->lane)))
					return false;
			}
			return true;
		}
		}
	}

	void ScoreEditorTimeline::noteResizeR(Note& note, EditArgs&)
	{
		switch (snapMode)
		{
		default:
			for (auto&& [_, pnote] : context.selectedNotes)
				pnote->width += shiftingLane;
			inputLane = note.lane + note.width;
			break;
		case SnapMode::Absolute:
			for (auto&& [_, pnote] : context.selectedNotes)
				pnote->width += shiftingLane;
			inputLane = mouseLane;
			break;
		case SnapMode::IndividualAbsolute:
		{
			auto snapFn = shiftingLane > 0 ? ceilf : floorf;
			float offset = snapFn(shiftingLane * laneDivision) / laneDivision;
			for (auto&& [_, pnote] : context.selectedNotes)
			{
				float width = snapFn(pnote->width * laneDivision) / laneDivision;
				if (pnote->width == width)
					pnote->width += offset;
				else
					pnote->width = width;
			}
			inputLane = mouseLane;
			break;
		}
		}
	}

	bool ScoreEditorTimeline::canNoteMove(const Note& note, EditArgs& edit)
	{
		switch (snapMode)
		{
		default:
		case SnapMode::Relative:
			shiftingLane = roundToStep(mouseLane - inputLane, laneDivision);
			shiftingTick = quartersToTicks(
			    roundToStep(ticksToQuarters(mouseTick - inputTick), quarterDivision));
			return context.canMoveNoteSelection(shiftingTick, quarterDivision, shiftingLane,
			                                    laneDivision, SnapMode::Relative);
		case SnapMode::Absolute:
		case SnapMode::IndividualAbsolute:
			shiftingLane =
			    roundToStep(mouseLane - note.width * edit.noteAlign, laneDivision) - note.lane;
			shiftingTick = -note.tick + quartersToTicks(roundToStep(ticksToQuarters(mouseTick),
			                                                        quarterDivision));
			if ((shiftingLane == 0 || (mouseLane - inputLane) == 0) &&
			    (shiftingTick == 0 || (mouseTick - inputTick) == 0))
				return false;
			// if (std::abs(shiftingLane) < std::abs(mouseLane - inputLane))
			//	shiftingLane = mouseLane - inputLane;
			// if (std::abs(shiftingTick) < std::abs(mouseTick - inputTick))
			//	shiftingTick = mouseTick - inputTick;
			return context.canMoveNoteSelection(shiftingTick, quarterDivision, shiftingLane,
			                                    laneDivision, snapMode);
		}
	}

	void ScoreEditorTimeline::noteMove(Note& note, EditArgs& edit)
	{
		isMovingNote = true;
		context.moveNoteSelection(shiftingTick, quarterDivision, shiftingLane, laneDivision,
		                          snapMode, false);
		switch (snapMode)
		{
		default:
			inputLane = note.lane + note.width * edit.noteAlign;
			inputTick = note.tick;
			break;
		case SnapMode::Absolute:
			inputLane = mouseLane;
			inputTick = mouseTick;
			break;
		case SnapMode::IndividualAbsolute:
			inputLane = mouseLane;
			inputTick = mouseTick;
			break;
		}
	}

	void ScoreEditorTimeline::noteSelector()
	{
		assert(drawSplitter._Count <= 1 && "drawSplitter is in used");
		if (ImGui::BeginPopup("NoteSelector", ImGuiWindowFlags_NoMove))
		{
			ImGuiIO& io = ImGui::GetIO();
			ImDrawList* popupDrawList = ImGui::GetWindowDrawList();
			ImGui::TextColored({ 0.8f, 0.8f, 0.8f, 1.f },
			                   (const char*)localize(Text::noteSelectTooltip));
			ImGui::Separator();
			float height = toTimeUnit(noteHeight);
			drawSplitter.Split(popupDrawList, Channel_Base + 1);
			for (const auto& pnote : context.hoveringNotes)
			{
				Note& note = *pnote;
				drawSplitter.SetCurrentChannel(popupDrawList, Channel_Base);
				ImGui::Text("Lane: %.2f | Width: %.2f", note.lane, note.width);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				auto&& [sLane, sTime] = fromScreen(cursor);
				Note dummy = note;
				dummy.lane = sLane;
				dummy.width = std::clamp(dummy.width, 0.5f, 6.f);
				dummy.tick = accumulateTicks(sTime - height / 2, context.score.tempoChanges);
				dummy.flag = setFlag(dummy.flag, NoteFlag::Attached, false);
				ImVec2 size = { toScreenWidth(dummy.width), noteHeight };
				ImGui::PushID(&note);
				if (ImGui::InvisibleButton("S", size))
				{
					if (io.KeyCtrl)
						context.selectNote(note, true);
					else
						context.deselectNote(note);
				}
				if (context.selection.has(note))
					popupDrawList->AddRect(cursor, cursor + size, 0xcccccccc, 2.0f,
					                       ImDrawFlags_RoundCornersAll, 2.0f);
				drawNote(popupDrawList, dummy, noteTint, DrawChannel(0), context.score);
				ImGui::PopID();
			}
			if ((!io.KeyCtrl && !io.KeyAlt) || context.hoveringNotes.empty())
				ImGui::CloseCurrentPopup();
			if (!ImGui::IsWindowHovered() && io.MouseClicked[ImGuiMouseButton_Left])
				// HACK: Prevent popup from closing
				io.MouseClicked[ImGuiMouseButton_Left] = false;
			drawSplitter.Merge(popupDrawList);
			ImGui::EndPopup();
		}
	}

	id_t ScoreEditorTimeline::findHoveringHoldNote()
	{
		for (auto&& [holdID, hold] : context.score.holdNotes)
		{
			const Note& startStep = context.score.notes.at(hold.steps.front());
			const Note& endStep = context.score.notes.at(hold.steps.back());

			if (startStep.tick > mouseTick || endStep.tick < mouseTick)
				continue;

			auto cmp = [&](tick_t tick, id_t noteID)
			{ return tick < context.score.notes.at(noteID).tick; };
			auto it = std::upper_bound(hold.joints.begin(), hold.joints.end(), mouseTick, cmp);
			if (it == hold.joints.end())
				--it;
			const Note& startNote = context.score.notes.at(*std::prev(it));
			const Note& endNote = context.score.notes.at(*it);
			if (!context.isLayerInteractive(startNote.layer) &&
			    !context.isLayerInteractive(endNote.layer))
				continue;
			float ratio = unlerp(startNote.tick, endNote.tick, mouseTick, 0.5f);
			EaseFunction easeFunc = getEaseFunction(startNote.ease);
			float X1 = easeFunc(startNote.lane, endNote.lane, ratio);
			float X2 =
			    easeFunc(startNote.lane + startNote.width, endNote.lane + endNote.width, ratio);
			if (isWithinRange(mouseLane, X1, X2))
				return holdID;
		}
		return -1;
	}

	bool ScoreEditorTimeline::eventControl(ImDrawList* drawList, const char* txt, ImU32 color,
	                                       float x, float y, bool enabled)
	{
		ImGui::PushID(static_cast<int>(y));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 1, 0 });
		ImVec2 pos = ImGui::GetCursorScreenPos();
		pos.y = y - ImGui::GetFrameHeightWithSpacing();
		ImGui::SetNextItemAllowOverlap();
		bool activated =
		    UI::coloredButton(txt, pos, { -1, ImGui::GetFrameHeightWithSpacing() }, color, enabled);
		ImGui::PopStyleVar();
		ImGui::PopID();

		float m = pos.x + ImGui::GetItemRectSize().x;
		drawList->AddLine({ x, y }, { m, y }, color, UI::scale(primaryLineThickness));
		return activated;
	}

	bool ScoreEditorTimeline::bpmControl(ImDrawList* drawList, const Tempo& tempo, bool enable)
	{
		float x = std::min(toScreenPosX(context.maxLane()), rightPanelScreenPos.x);
		float y = toScreenPosY(accumulateDuration(tempo.tick, context.score.tempoChanges));
		std::string bpmStr = IO::formatString("%g BPM", tempo.quarterPerMinute);
		return eventControl(drawList, bpmStr.c_str(), tempoColor, x, y, enable);
	}

	bool ScoreEditorTimeline::timeSignatureControl(ImDrawList* drawList, const TimeSignature& ts,
	                                               bool enabled)
	{
		float x = std::min(toScreenPosX(context.maxLane()), rightPanelScreenPos.x);
		tick_t tick = accumulateTicks(ts.measure, context.score.timeSignatures);
		float y = toScreenPosY(accumulateDuration(tick, context.score.tempoChanges));
		std::string tsStr = IO::formatString("%d/%d", ts.numerator, ts.denominator);
		return eventControl(drawList, tsStr.c_str(), timeColor, x, y, enabled);
	}

	bool ScoreEditorTimeline::hiSpeedControl(ImDrawList* drawList, const HiSpeed& hispeed,
	                                         bool selected, bool enabled)
	{
		bool showLayerName = isArrayIndexInBounds(hispeed.layer, context.score.layers) &&
		                     context.selectedLayer != hispeed.layer;
		std::string speedStr = hispeed.ease == HiSpeedEaseType::Linear ? "^" : "";
		IO::formatFixedFloatTrimmed(speedStr, hispeed.speed, 4);
		speedStr += "x";
		if (!isClose(hispeed.skips, 0.0f, FLT_EPSILON * 1000))
			IO::formatFixedFloatTrimmed(speedStr, hispeed.skips, 4, "%+.*f");
		if (showLayerName)
		{
			auto& layerName = context.score.layers[hispeed.layer].name;
			speedStr += " (";
			speedStr += layerName.size() > 12 ? (layerName.substr(0, 12) + "..") : layerName;
			speedStr += ")";
		}
		bool isInteractive = context.isLayerInteractive(hispeed.layer);
		ImU32 color = hispeed.hideNotes ? (isInteractive ? hideSpeedColor : inactiveHideSpeedColor)
		                                : (isInteractive ? speedColor : inactiveSpeedColor);
		if (selected)
		{
			ImVec4 u4color = ImGui::ColorConvertU32ToFloat4(color);
			color = ImGui::ColorConvertFloat4ToU32(generateHighlightColor(u4color));
		}
		float x = std::min(toScreenPosX(context.maxLane()), rightPanelScreenPos.x);
		float y = toScreenPosY(accumulateDuration(hispeed.tick, context.score.tempoChanges));
		ImGui::PushID(hispeed.tick);
		bool activated = eventControl(drawList, speedStr.c_str(), color, x, y, enabled);
		ImGui::PopID();
		if (selected)
		{
			ImRect rect = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
			rect.Expand(2);
			drawList->AddRect(rect.Min, rect.Max, 0xffcccccc, 2);
		}
		return activated;
	}

	bool ScoreEditorTimeline::skillControl(ImDrawList* drawList, const Skill& skill, bool enabled)
	{
		float x =
		    std::max(toScreenPosX(context.minLane()), leftPanelScreenPos.x + panelScreenSize.x);
		float y = toScreenPosY(accumulateDuration(skill.tick, context.score.tempoChanges));

		if (context.metadata.isExtendedScore)
		{
			const char* shortEffect[] = { "S", "H", "AP" };
			std::string text = IO::formatString("%s [%s.%d]", (const char*)localize(Text::skill),
			                                    shortEffect[(int)skill.effect], (int)skill.level);
			return eventControl(drawList, text.c_str(), skillColor, x, y, enabled);
		}
		else
			return eventControl(drawList, localize(Text::skill), skillColor, x, y, enabled);
	}

	bool ScoreEditorTimeline::feverControl(ImDrawList* drawList, const Fever& fever, bool enabled)
	{
		if (fever.startTick < 0 || fever.endTick < 0 || fever.endTick < fever.startTick)
			return false;
		const char* startStr = "FEVER" ICON_FA_CARET_UP;
		const char* endStr = "FEVER" ICON_FA_CARET_DOWN;
		float x =
		    std::max(toScreenPosX(context.minLane()), leftPanelScreenPos.x + panelScreenSize.x);
		float begY = toScreenPosY(accumulateDuration(fever.startTick, context.score.tempoChanges));
		float endY = toScreenPosY(accumulateDuration(fever.endTick, context.score.tempoChanges));
		bool activated = false;
		activated |= eventControl(drawList, startStr, feverColor, x, begY, enabled);
		ImVec2 end = ImGui::GetItemRectMin();
		activated |= eventControl(drawList, endStr, feverColor, x, endY, enabled);
		ImVec2 start = ImGui::GetItemRectMax();
		const float LINE_WIDTH = 2;
		float centerX = start.x + (end.x - start.x) / 2;
		drawList->AddLine({ centerX, start.y }, { centerX, end.y }, feverColor, LINE_WIDTH);
		return activated;
	}

	bool ScoreEditorTimeline::waypointControl(ImDrawList* drawList, const Waypoint& waypoint,
	                                          bool enabled)
	{
		float x =
		    std::max(toScreenPosX(context.minLane()), leftPanelScreenPos.x + panelScreenSize.x);
		float y = toScreenPosY(accumulateDuration(waypoint.tick, context.score.tempoChanges));
		ImGuiStyle& style = ImGui::GetStyle();
		float size = std::max(ImGui::CalcTextSize(waypoint.name.c_str()).x + 6.0f, 31.0f);
		float avail = ImGui::GetContentRegionAvail().x;
		if (avail > size)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - size));
		return eventControl(drawList, waypoint.name.c_str(), waypointColor, x, y, enabled);
	}

	Tempo ScoreEditorTimeline::getPreviewTempo(const EditArgs& edit) const
	{
		return Tempo{ snapTick, edit.bpm };
	}

	TimeSignature ScoreEditorTimeline::getPreviewTimeSignatrue(const EditArgs& edit) const
	{
		return TimeSignature{ accumulateMeasures(snapTick, context.score.timeSignatures),
			                  edit.timeSignatureNumerator, edit.timeSignatureDenominator };
	}

	HiSpeed ScoreEditorTimeline::getPreviewHispeed(const EditArgs& edit) const
	{
		if (!context.metadata.isExtendedScore)
			return HiSpeed{ snapTick, -1, edit.hiSpeed };
		return HiSpeed{ snapTick,         -1,
			            edit.hiSpeed,     edit.hiSpeedSkip,
			            edit.hiSpeedEase, edit.hiSpeedHideNotes };
	}

	void ScoreEditorTimeline::updateTimelineOffset()
	{
		// Update timeline offset like visualOffset, targetOffset, maxTime
		const float minLaneOffset = context.minLane() - 1;
		const float maxLaneOffset = context.maxLane() - NUM_LANES + 1;
		targetOffset.x = std::clamp(targetOffset.x, minLaneOffset, maxLaneOffset);
		const float minOffsetY =
		    UNIT_Y / zoomY / 2 - ORIGIN_Y - toTimeUnit(TIMELINE_SCREEN_Y_MIN_OFFSET);
		targetOffset.y = std::max(targetOffset.y, minOffsetY);
		maxTime = std::max(UNIT_Y / zoomY / 2.0 + ORIGIN_Y + targetOffset.y, maxTime);
		if (getConfig().useSmoothScrolling && !snapToTargetTime)
		{
			ImVec2 scrolling = targetOffset - visualOffset;
			float scrollSpeed = getConfig().smoothScrollingTime / (ImGui::GetIO().DeltaTime * 1000);
			ImVec2 delta = scrolling / scrollSpeed;
			visualOffset.x += std::copysign(
			    std::max(std::abs(delta.x), std::min(0.01f, std::abs(scrolling.x))), delta.x);
			visualOffset.y += std::copysign(
			    std::max(std::abs(delta.y), std::min(0.05f, std::abs(scrolling.y))), delta.y);
		}
		else
		{
			visualOffset = targetOffset;
			snapToTargetTime = false;
		}

		// Update timeline values only after updating visualOffset
		// Do not change visualOffset after calculating this
		timelinePos = fromScreen(timelineScreenPos + ImVec2{ 0, timelineScreenSize.y });
		timelineSize = { UNIT_X / zoomX, UNIT_Y / zoomY };
		ImVec2 absMousePos = ImGui::GetMousePos();
		if (ImGui::IsMousePosValid(&absMousePos))
		{
			mouseLane = toLanePos(absMousePos.x);
			float mouseTime = std::max(toTimePos(absMousePos.y), 0.0);
			mouseTick = accumulateTicks(mouseTime, context.score.tempoChanges);

			measure_t measure = accumulateMeasures(mouseTick, context.score.timeSignatures);
			const TimeSignature& currentTS = getTimeSignAt(context.score, measure);

			qnote_t measureDivision = 1 / quatersPerMeasure(currentTS);
			if (quarterDivision > measureDivision)
			{
				snapTick =
				    quartersToTicks(roundToStep(ticksToQuarters(mouseTick), quarterDivision));
			}
			else
			{
				tick_t tickPerMeasure = TICKS_PER_QUARTER / measureDivision;
				tick_t measureTick = accumulateTicks(measure, context.score.timeSignatures);
				tick_t tickOffset = (mouseTick - measureTick) % tickPerMeasure;
				tickOffset -= (tickOffset > tickPerMeasure / 2 ? tickPerMeasure : 0);
				snapTick = mouseTick - tickOffset;
			}
			snapTick = std::max(snapTick, 0);
		}
	}

	void ScoreEditorTimeline::updateContextMenu(PasteData& pasteData)
	{
		auto& input = getConfig().input;
		if (ImGui::BeginPopupContextWindow(ImGui::GetCurrentWindowRead()->Name))
		{
			if (ImGui::MenuItem(localize(Text::del), ToShortcutString(input.deleteSelection), false,
			                    !context.selection.empty()))
				context.deleteSelection();

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::cut), ToShortcutString(input.cutSelection), false,
			                    !context.selection.empty()))
				context.cutSelection();

			if (ImGui::MenuItem(localize(Text::copy), ToShortcutString(input.copySelection), false,
			                    !context.selection.empty()))
				context.copySelection();

			if (ImGui::MenuItem(localize(Text::paste), ToShortcutString(input.paste)))
				pasteData.startPaste();

			if (ImGui::MenuItem(localize(Text::flipPaste), ToShortcutString(input.flipPaste)))
			{
				pasteData.startPaste();
				pasteData.flip();
			}

			if (ImGui::MenuItem(localize(Text::duplicate), ToShortcutString(input.duplicate), false,
			                    !context.selection.empty()))
			{
				context.copySelection();
				pasteData.startPaste();
			}

			if (ImGui::MenuItem(localize(Text::flipDuplicate),
			                    ToShortcutString(input.flipDuplicate), false,
			                    !context.selection.empty()))
			{
				context.copySelection();
				pasteData.startPaste();
				pasteData.flip();
			}

			if (ImGui::MenuItem(localize(Text::flip), ToShortcutString(input.flip), false,
			                    !context.selection.emptyNotes()))
				context.flipSelection();

			ImGui::Separator();
			EaseType ease;
			if (UI::selectMenuItems(Text::easeType,
			                        hasFlag(context.selectedFlag, SelectionFlag::CanEase),
			                        easeTypeTexts, ease, context.maxEase()))
				context.setEase(ease);

			EditHoldStepType step;
			if (UI::selectMenuItems(Text::stepType,
			                        context.metadata.isExtendedScore ||
			                            hasFlag(context.selectedFlag, SelectionFlag::HasHoldNote),
			                        stepTypeTexts, step, EditHoldStepType::HoldStepTypeCount))
				context.setStep(step);

			FlickType flick;
			if (UI::selectMenuItems(Text::flickType,
			                        hasFlag(context.selectedFlag, SelectionFlag::CanFlick),
			                        flickTypeTexts, flick, context.maxFlick()))
				context.setFlick(flick);

			FadeType fade;
			if (context.metadata.isExtendedScore &&
			    UI::selectMenuItems(Text::fadeType,
			                        hasFlag(context.selectedFlag, SelectionFlag::HasGuideNote),
			                        fadeTypeTexts, fade, FadeType::FadeTypeCount))
				context.setFadeType(fade);

			if (context.metadata.isExtendedScore)
			{
				GuideColor color;
				if (UI::selectMenuItems(Text::guideColor,
				                        hasFlag(context.selectedFlag, SelectionFlag::HasGuideNote),
				                        guideColorAllTexts, color, GuideColor::GuideColorCount))
					context.setGuideColor(color);
			}
			else
			{
				ClassicGuideColor color;
				if (UI::selectMenuItems(Text::guideColor,
				                        hasFlag(context.selectedFlag, SelectionFlag::HasGuideNote),
				                        guideColorTexts, color, ClassicGuideColor::GuideColorCount))
					context.setGuideColor(color == ClassicGuideColor::Green ? GuideColor::Green
					                                                        : GuideColor::Yellow);
			}

			if (context.metadata.isExtendedScore &&
			    ImGui::BeginMenu(localize(Text::layer), !context.selection.empty()))
			{
				for (size_t i = 0; i < context.score.layers.size(); ++i)
					if (ImGui::MenuItem(context.score.layers[i].name.c_str()))
						context.setLayer(i);
				ImGui::EndMenu();
			}

			SoundEffectType soundEffect;
			if (context.metadata.isExtendedScore &&
			    UI::selectMenuItems(Text::soundEffect,
			                        hasFlag(context.selectedFlag, SelectionFlag::CanSoundEffect),
			                        soundEffectTexts, soundEffect))
				context.setSoundEffect(soundEffect);

			ImGui::Separator();
			bool canShrink =
			    (context.selectedNotes.size() + context.selection.hispeeds.size()) >= 2;
			if (ImGui::MenuItem(localize(Text::shrinkUp), NULL, false, canShrink))
				context.shrinkSelection(-1);

			if (ImGui::MenuItem(localize(Text::shrinkDown), NULL, false, canShrink))
				context.shrinkSelection(1);

			if (ImGui::MenuItem(localize(Text::compressSelection), NULL, false,
			                    context.selectedNotes.size() >= 2))
				context.compressSelection();

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::connectHolds), NULL, false,
			                    hasFlag(context.selectedFlag, SelectionFlag::CanConnectHold)))
				context.connectHoldsInSelection();

			if (ImGui::MenuItem(localize(Text::splitHold), NULL, false,
			                    hasFlag(context.selectedFlag, SelectionFlag::HasAnyHoldMid) &&
			                        context.selectedNotes.size() == 1))
				context.splitHoldInSelection();

			if (ImGui::BeginMenu(localize(Text::holdToTraces),
			                     hasFlag(context.selectedFlag, SelectionFlag::HasAnyHoldNoteStep)))
			{
				if (ImGui::MenuItem(localize(Text::addTracesForHold)))
					context.convertHoldToTraces(quarterDivision, false);
				if (ImGui::MenuItem(localize(Text::convertHoldToTraces)))
					context.convertHoldToTraces(quarterDivision, true);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(localize(Text::convertGuideHold),
			                     hasFlag(context.selectedFlag, SelectionFlag::HasAnyHoldNoteStep)))
			{
				if (ImGui::BeginMenu(localize(Text::convertGuideToHold)))
				{
					if (ImGui::MenuItem(localize(Text::none)))
						context.convertGuideToHold(false);
					if (ImGui::MenuItem(localize(Text::critical)))
						context.convertGuideToHold(true);
					ImGui::EndMenu();
				}

				if (context.metadata.isExtendedScore)
				{
					GuideColor color;
					if (UI::selectMenuItems(Text::convertHoldToGuide, true, guideColorAllTexts,
					                        color))
						context.convertHoldToGuide(color);
					if (ImGui::MenuItem(localize(Text::convertHoldToNone)))
						context.convertHoldToNone();
				}
				else
				{
					ClassicGuideColor color;
					if (UI::selectMenuItems(Text::convertHoldToGuide, true, guideColorTexts, color))
						context.convertHoldToGuide(color == ClassicGuideColor::Yellow
						                               ? GuideColor::Yellow
						                               : GuideColor::Green);
				}

				ImGui::EndMenu();
			}
			ImGui::Separator();

			if (UI::selectMenuItems(Text::lerpHispeeds, context.selection.hispeeds.size() >= 2,
			                        easeTypeTexts, ease))
				context.lerpHiSpeeds(quarterDivision, ease);

			ImGui::EndPopup();
		}
	}

	void ScoreEditorTimeline::updateStatusBar()
	{
		const auto& config = getConfig();
		ImGui::SetCursorScreenPos(toolbarScreenPos);

		if (UI::transparentButton(ICON_FA_BACKWARD, UI::btnSmall, true, !playing && curTime > 0))
			jumpToPrevDivision();

		ImGui::SameLine();
		if (UI::transparentButton(ICON_FA_STOP, UI::btnSmall, false))
			stop();

		ImGui::SameLine();
		if (UI::transparentButton(playing ? ICON_FA_PAUSE : ICON_FA_PLAY, UI::btnSmall))
			setPlaying(!playing);

		ImGui::SameLine();
		if (UI::transparentButton(ICON_FA_FORWARD, UI::btnSmall, true, !playing))
			jumpToNextDivision();

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);

		ImGui::SameLine();
		if (config.divisionType == (int)TimeDivisionType::Quarter)
			ImGui::TextUnformatted(localize(Text::beat));
		else
			ImGui::TextUnformatted(localize(Text::measure));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(85);
		if (config.divisionType == (int)TimeDivisionType::Quarter)
			UI::divisionSelect("quarterDivision", quarterDivision, QUART_DIVISIONS,
			                   std::size(QUART_DIVISIONS));
		else
		{
			int measureDivision = quarterDivision * 4;
			UI::divisionSelect("measureDivison", measureDivision, MEASURE_DIVISIONS,
			                   std::size(MEASURE_DIVISIONS));
			quarterDivision = measureDivision / 4;
		}

		ImGui::SameLine(0, 2);
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);

		if (context.metadata.isExtendedScore)
		{
			ImGui::SameLine(0, 3);
			ImGui::TextUnformatted(localize(Text::lane));

			ImGui::SameLine();
			ImGui::SetNextItemWidth(80);
			int laneDiv = laneDivision * 12;
			if (UI::divisionSelect("laneDivision", laneDiv, LANE_DIVISIONS,
			                       std::size(LANE_DIVISIONS)))
				laneDivision = laneDiv / 12.f;

			ImGui::SameLine(0, 2);
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		}

		ImGui::SameLine(0, 3);
		if (ImGui::BeginCombo("##snap_combo", localize(snapModeTexts[(int)snapMode]),
		                      ImGuiComboFlags_WidthFitPreview))
		{
			for (auto&& mode : EnumRange(SnapMode::SnapModeMax))
			{
				if (ImGui::Selectable(localize(snapModeTexts[(int)mode]), snapMode == mode))
					snapMode = mode;
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);

		tick_t currentTick = getCurrentTick();
		measure_t currentMeasure = getCurrentMeasure();
		const TimeSignature& ts = getTimeSignAt(context.score, currentMeasure);
		const Tempo& tempo = getTempoAt(context.score, currentTick);
		float bpm = tempo.quarterPerMinute / quatersPerMeasure(ts) * beatsPerMeasure(ts);
		const HiSpeed* hiSpeedPtr = getHiSpeedAt(context.score, currentTick, context.selectedLayer);
		float speed = hiSpeedPtr ? hiSpeedPtr->speed : 1.0f;

		float time, ms = std::modf(curTime, &time) * 100;
		div_t timeDiv = std::div(int(time), 60);
		char rhythmStringBuf[128];
		int length = std::snprintf(rhythmStringBuf, std::size(rhythmStringBuf),
		                           " %02d:%02d:%02d  |  %dt  |  %d/%d  |  %g BPM  |  %gx ",
		                           timeDiv.quot, timeDiv.rem, (int)ms, currentTick, ts.numerator,
		                           ts.denominator, bpm, speed);
		float strWidth =
		    length > 0 ? ImGui::CalcTextSize(rhythmStringBuf, rhythmStringBuf + length).x : 0;

		float _zoom = zoomY;
		ImGui::SameLine();
		int controlWidth = ImGui::GetContentRegionAvail().x - strWidth - (UI::btnSmall.x * 3);
		if (UI::zoomControl("zoom", _zoom, ZOOM_Y_MIN, ZOOM_Y_MAX,
		                    std::clamp(controlWidth, 100, 400), ZOOM_Y_FACTOR))
			setZoomY(_zoom, 0.5f);

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		if (length > 0)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(rhythmStringBuf, rhythmStringBuf + length);
		}
	}

	void ScoreEditorTimeline::updateScrollBar(ImDrawList* drawList)
	{
		if (!drawList)
			return;

		ImGuiStyle& style = ImGui::GetStyle();
		ImGuiIO& io = ImGui::GetIO();

		float scrollBarLeft = rightPanelScreenPos.x + panelScreenSize.x;
		float scrollBarRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
		scrollBarLeft =
		    std::round(scrollBarLeft + (scrollBarRight - scrollBarLeft - style.ScrollbarSize) / 2);
		scrollBarRight = scrollBarLeft + style.ScrollbarSize;
		ImRect scrollBB = { scrollBarLeft, timelineScreenPos.y, scrollBarRight,
			                timelineScreenPos.y + timelineScreenSize.y };

		// calculate handle height
		float minTime = -toTimeUnit(TIMELINE_SCREEN_Y_MIN_OFFSET);
		float totalTime = maxTime - minTime;
		float grabHeight = std::max(timelineSize.y / totalTime * scrollBB.GetHeight(), 24.f);

		// calculate handle position
		float targetPos = -UNIT_Y / zoomY / 2 + ORIGIN_Y + targetOffset.y;
		float positionRatio =
		    totalTime > timelineSize.y ? (targetPos - minTime) / (totalTime - timelineSize.y) : 1;
		float grabTop = lerpD(scrollBB.Min.y, scrollBB.Max.y - grabHeight, 1 - positionRatio);

		// make handle slightly narrower than the background
		ImRect grabBB = { scrollBarLeft, grabTop, scrollBarRight, grabTop + grabHeight };
		grabBB.Expand(-style.ScrollbarPadding);

		// Scroll bar
		bool active = false;
		ImGuiCol grabColor = ImGuiCol_ScrollbarGrab;
		ImGui::SetCursorScreenPos(scrollBB.Min);
		ImGui::PushID(ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindowRead(), ImGuiAxis_Y));
		ImGui::InvisibleButton("", scrollBB.GetSize(), ImGuiButtonFlags_NoNavFocus);
		ImGui::PopID();

		if (ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(grabBB.Min, grabBB.Max))
			grabColor = ImGuiCol_ScrollbarGrabHovered;

		if (ImGui::IsItemActive())
		{
			grabColor = ImGuiCol_ScrollbarGrabActive;
			float newRatio = unlerp(scrollBB.Max.y - grabHeight / 2,
			                        scrollBB.Min.y + grabHeight / 2, io.MousePos.y);
			targetOffset.y = UNIT_Y / zoomY / 2 - ORIGIN_Y + minTime +
			                 (totalTime - timelineSize.y) * std::clamp(newRatio, 0.f, 1.f);
		}

		ImU32 scrollBgColor = ImGui::GetColorU32(ImGuiCol_ScrollbarBg);
		ImU32 scrollGrabColor = ImGui::GetColorU32(grabColor);
		drawList->AddRectFilled(scrollBB.Min, scrollBB.Max, scrollBgColor);
		drawList->AddRectFilled(grabBB.Min, grabBB.Max, scrollGrabColor, style.ScrollbarRounding);
	}

	void ScoreEditorTimeline::updatePlayback()
	{
		if (!playing)
			return;
		context.audio->syncSoundEffectsProfile();

		playingNoteSounds.clear();
		secs_t endWindow = curTime + AUDIO_LOOK_AHEAD * playbackSpeed;
		secs_t startWindow = curTime != lastFrameTime
		                         ? lastFrameTime + AUDIO_LOOK_AHEAD * playbackSpeed
		                         : lastFrameTime;
		tick_t startTick = accumulateTicks(startWindow, context.score.tempoChanges);
		tick_t endTick = accumulateTicks(endWindow, context.score.tempoChanges);
		for (auto it = context.notesOrderedView.lower_bound(startTick),
		          end = context.notesOrderedView.lower_bound(endTick);
		     it != end; ++it)
		{
			const auto& [_, pnote] = *it;
			if (pnote->isDummy())
				continue;

			std::string_view se = getNoteSE(*pnote);
			if (se.empty())
				continue;

			std::string key = IO::formatString("%d-%s", pnote->tick, se.data());
			if (playingNoteSounds.find(key) == playingNoteSounds.end())
			{
				secs_t startTime = accumulateDuration(pnote->tick, context.score.tempoChanges);
				context.audio->playSoundEffect(se.data(), startTime - AUDIO_CORRECTION_OFFSET, -1,
				                               curTime);
				playingNoteSounds.insert(key);
			}
		}

		for (const auto& [_, hold] : context.score.holdNotes)
		{
			const Note& front = context.score.notes.at(hold.steps.front());
			const Note& back = context.score.notes.at(hold.steps.back());
			if (front.tick > endTick || back.tick < startTick)
				continue;
			for (auto separatorIt = hold.separators.begin(), nextIt = std::next(separatorIt);
			     separatorIt != nextIt;)
			{
				const HoldNoteStep& step = *separatorIt;
				id_t endID;
				if (step.isDummy() || step.isGuide())
				{
					separatorIt = nextIt;
					if (nextIt != hold.separators.end())
						++nextIt;
					continue;
				}
				else if (nextIt == hold.separators.end())
				{
					endID = hold.steps.back();
					separatorIt = nextIt;
				}
				else if (nextIt->isDummy() || nextIt->isGuide())
				{
					endID = nextIt->ID;
					separatorIt = nextIt;
				}
				else
				{
					++nextIt;
					continue;
				}
				const Note& start = context.score.notes.at(step.ID);
				const Note& end = context.score.notes.at(endID);
				if (start.tick > endTick || end.tick < startTick)
					continue;
				secs_t startTime = accumulateDuration(start.tick, context.score.tempoChanges);
				secs_t endTime = accumulateDuration(end.tick, context.score.tempoChanges);
				context.audio->playSoundEffect(step.isCrit() ? SE_CRITICAL_CONNECT : SE_CONNECT,
				                               startTime - AUDIO_CORRECTION_OFFSET,
				                               endTime - AUDIO_CORRECTION_OFFSET, curTime);
			}
		}

		lastFrameTime = curTime;
		curTime += ImGui::GetIO().DeltaTime * playbackSpeed;

		secs_t stopTime = getStopTime();
		bool playedBeforeEnd = prevTime < stopTime;
		if (getConfig().stopPlaybackAtMusicEnd && playedBeforeEnd && curTime >= stopTime)
		{
			setPlaying(false, false);
		}
		if (getConfig().followCursorInPlayback &&
		    curTime > (timelinePos.y + timelineSize.y * getConfig().cursorPositionThreshold))
		{
			scrollToCursor(1);
			snapToTargetTime = true;
		}
		// Keep cursor on screen
		else if (curTime > (timelinePos.y + timelineSize.y))
		{
			scrollTo(curTime, 0);
		}
	}

	void ScoreEditorTimeline::updateNotes(ImDrawList* drawList, EditArgs& edit,
	                                      PasteData& pasteData)
	{
		if (!drawList)
			return;
		if (!ImGui::IsPopupOpen("NoteSelector"))
			context.hoveringNotes.clear();
		drawSplitter.Split(drawList, Channel_Count);

		float visibleExtend = toTimeUnit(noteHeight / 2);
		// Start a little more early so flick note doesn't disappear
		secs_t startTime = std::max(timelinePos.y - visibleExtend * 2.6f, 0.f);
		tick_t startTick = accumulateTicks(startTime, context.score.tempoChanges);
		tick_t endTick = accumulateTicks(timelinePos.y + timelineSize.y + visibleExtend,
		                                 context.score.tempoChanges);
		auto noteStart = context.notesOrderedView.lower_bound({ startTick });
		auto noteEnd = context.notesOrderedView.upper_bound({ endTick });

		for (auto it = noteStart; it != noteEnd; ++it)
		{
			Note& note = *it->second;
			if (!context.isLayerVisible(note.layer))
				continue;
			if (context.selection.has(note))
				// Selected notes are *highlighted* so they are rendered later
				continue;
			updateNote(note, edit);
			const Color& tint = context.isLayerInteractive(note.layer) ? noteTint : otherLayerTint;
			int channel = context.isLayerSelected(note.layer) ? Channel_Base : 0;
			drawNote(drawList, note, tint, DrawChannel(channel), context.score);
		}

		for (auto&& [ID, pnote] : context.selectedNotes)
		{
			Note& note = *pnote;
			if (note.tick < startTick || note.tick > endTick)
			{
				if (grabbingNote == ID)
					updateNote(note, edit);
				continue;
			}
			updateNote(note, edit);
			const Color& tint = context.isLayerInteractive(note.layer) ? noteTint : otherLayerTint;
			int channel = context.isLayerSelected(note.layer) ? Channel_Base : 0;
			drawNote(drawList, note, tint, DrawChannel(channel), context.score);
		}

		for (auto&& [_, hold] : context.score.holdNotes)
		{
			const auto canDraw = [](const Note& note, void* args)
			{
				ScoreContext& context = *static_cast<ScoreContext*>(args);
				return context.isLayerVisible(note.layer);
			};
			const auto getTint = [](const Note& note, void* args)
			{
				ScoreContext& context = *static_cast<ScoreContext*>(args);
				return context.isLayerInteractive(note.layer) ? noteTint : otherLayerTint;
			};
			const auto getChannel = [](const Note& note, void* args)
			{
				ScoreContext& context = *static_cast<ScoreContext*>(args);
				return context.isLayerSelected(note.layer) ? Channel_Base : DrawChannel(0);
			};
			drawHoldNote(drawList, hold, context.score, startTick, endTick, canDraw, getTint,
			             getChannel, &context);
		}

		if (pasteData.pasting && !(isInsertingNote || isInsertingHold) && mouseInTimeline &&
		    !playing)
		{
			float noteWidth =
			    std::clamp(pasteData.width, context.minNoteWidth(), context.maxNoteWidth());
			float snapLane = roundToStep(mouseLane - noteWidth / 2, laneDivision);
			float noteLane = std::clamp(snapLane, context.minLane(), context.maxLane(noteWidth));
			float offsetLane = noteLane - pasteData.minLane;
			float offsetTick = snapTick;

			auto pasteNoteEnd = pasteData.notesOrderedView.upper_bound(endTick - snapTick);
			for (auto noteIt = pasteData.notesOrderedView.begin(); noteIt != pasteNoteEnd; ++noteIt)
			{
				const Note& note = *noteIt->second;
				drawNote(drawList, note, hoverTint, Channel_Hover, pasteData, offsetLane,
				         offsetTick);
			}

			for (const auto& [_, hold] : pasteData.holdNotes)
			{
				const auto canDraw = [](const Note& note, void*) { return true; };
				const auto getTint = [](const Note& note, void*) { return hoverTint; };
				const auto getChannel = [](const Note& note, void*) { return Channel_Hover; };
				drawHoldNote(drawList, hold, pasteData, startTick, endTick, canDraw, getTint,
				             getChannel, &context, offsetLane, offsetTick);
			}

			if (mouseClicked)
			{
				context.paste(pasteData, offsetLane, offsetTick,
				              ImGui::GetIO().KeyShift ? -1 : findHoveringHoldNote());
				// claim the mouse click
				mouseClicked = false;
			}
		}

		if (isInsertingNote || isInsertingHold ||
		    (mouseInTimeline && !playing && !pasteData.pasting && grabbingNote < 0 &&
		     edit.isNoteInsertMode() && !UI::isAnyPopupOpen()))
		{
			updatePreviewNote(edit);
			if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				const Color& tint = isInsertingNote ? noteTint : hoverTint;
				switch (edit.insertMode)
				{
				case InsertMode::InsertTap:
				case InsertMode::InsertFlick:
				case InsertMode::InsertLongMid:
				case InsertMode::MakeCritical:
				case InsertMode::MakeFriction:
				case InsertMode::InsertDamage:
				case InsertMode::MakeDummy:
					drawNote(drawList, previewNotes.notes[0], tint, Channel_Hover, previewNotes);
					break;
				case InsertMode::InsertLong:
				case InsertMode::InsertGuide:
				{
					const Color& tintPlaced = isInsertingHold ? noteTint : tint;
					const HoldNote& hold = previewNotes.holdNotes[0];
					const Note& start = previewNotes.notes[hold.steps.front()];
					const Note& end = previewNotes.notes[hold.steps.back()];
					drawNote(drawList, start, tintPlaced, Channel_Hover, previewNotes);
					if (!isInsertingHold)
						break;
					drawNote(drawList, end, tint, Channel_Hover, previewNotes);
					drawHoldCurve(drawList, start, end, hold.separators.front(), 0, 1,
					              hoverTint.scaleAlpha(start.guideAlpha),
					              hoverTint.scaleAlpha(end.guideAlpha), Channel_Hover);
				}
				break;
				default:
					drawList->AddText(ImGui::GetMousePos() - ImGui::CalcTextSize("?"),
					                  IM_COL32_WHITE, "?");
					break;
				}
			}
		}

		drawSplitter.Merge(drawList);
	}

	void ScoreEditorTimeline::updateNote(Note& note, EditArgs& edit)
	{
		if (!context.isLayerInteractive(note.layer))
			return;

		float height = noteHeight;
		float width = toScreenWidth(note.width);
		float centerX = toScreenPosX(note.lane) + width / 2;
		float top =
		    toScreenPosY(accumulateDuration(note.tick, context.score.tempoChanges)) - height / 2;
		float centerWidth = std::max(width / 2 - NOTE_CTRL_WIDTH, NOTE_CTRL_WIDTH / 2);
		float left = centerX - centerWidth - NOTE_CTRL_WIDTH;
		float right = centerX + centerWidth + NOTE_CTRL_WIDTH;

		ImGui::PushID(&note);
		if (mouseInTimeline && !UI::isAnyPopupOpen() &&
		    ImGui::IsMouseHoveringRect({ left, top }, { right, top + height }, false))
			context.hoveringNotes.push_back(&note);

		// Left resize
		noteControl("L", note, { left, top }, { NOTE_CTRL_WIDTH, height }, edit,
		            ImGuiMouseCursor_ResizeEW, &ScoreEditorTimeline::canNoteResizeL,
		            &ScoreEditorTimeline::noteResizeL);

		// Move
		noteControl("M", note, { left + NOTE_CTRL_WIDTH, top }, { centerWidth * 2, height }, edit,
		            ImGuiMouseCursor_Hand, &ScoreEditorTimeline::canNoteMove,
		            &ScoreEditorTimeline::noteMove);

		// Per note options here
		if (ImGui::IsItemDeactivated())
		{
			if (!isMovingNote && !context.selection.emptyNotes())
			{
				const bool set = ImGui::GetIO().KeyShift;
				const int setBoolean = set ? true : UNDEFINED;
				switch (edit.insertMode)
				{
				case InsertMode::InsertFlick:
					context.setFlick(set ? edit.flickType : FlickType::FlickTypeCount);
					break;

				case InsertMode::MakeCritical:
					context.setCriticals(setBoolean);
					break;

				case InsertMode::InsertLong:
					context.setEase(set ? edit.easeType : EaseType::EaseTypeCount);
					break;

				case InsertMode::InsertLongMid:
					context.setStep(set ? edit.stepType : EditHoldStepType::HoldStepTypeCount);
					break;

				case InsertMode::InsertGuide:
					context.setGuideColor(set ? edit.colorType : GuideColor::GuideColorCount);
					break;

				case InsertMode::MakeFriction:
					context.setFriction(setBoolean);
					break;

				case InsertMode::MakeDummy:
					context.setDummy(setBoolean);
					break;

				default:
					break;
				}
			}

			isMovingNote = false;
		}

		// Right resize
		noteControl("R", note, { right - NOTE_CTRL_WIDTH, top }, { NOTE_CTRL_WIDTH, height }, edit,
		            ImGuiMouseCursor_ResizeEW, &ScoreEditorTimeline::canNoteResizeR,
		            &ScoreEditorTimeline::noteResizeR);

		ImGui::PopID();
	}

	void ScoreEditorTimeline::updatePreviewNote(EditArgs& edit)
	{
		constexpr id_t defHoldID = 0;
		HoldNote& previewHold = previewNotes.holdNotes[defHoldID];
		if (previewNotes.notes.empty())
		{
			Note& n0 = previewNotes.notes[0];
			Note& n1 = previewNotes.notes[1];
			Note& n2 = previewNotes.notes[2];
			n0.ID = 0, n1.ID = 1, n2.ID = 2;
			n1.holdID = n2.holdID = defHoldID;
			previewHold.ID = defHoldID;
			previewHold.steps.push_back(1);
			previewHold.steps.push_back(2);
			previewHold.joints.push_back(1);
			previewHold.joints.push_back(2);
			previewHold.separators.push_back(HoldNoteStep{ 1 });
		}
		Note& previewNote = previewNotes.notes[0];
		previewNote.type = NoteType::Tap;
		previewNote.flick = FlickType::None;
		previewNote.flag = NoteFlag::None;
		Note& noteStart = previewNotes.notes[previewHold.steps.front()];
		Note& noteEnd = previewNotes.notes[previewHold.steps.back()];
		id_t inputNoteID = 0;
		switch (edit.insertMode)
		{
		case InsertMode::InsertLong:
		{
			HoldNoteStep& holdStep = previewHold.separators.front();
			holdStep.flag = HoldNoteFlag::Normal;
			holdStep.guideColor = GuideColor::Green;
			holdStep.layer = edit.holdLayer;
		}
			inputNoteID = !isInsertingHold ? 1 : 2;
			previewHold.fadeType = FadeType::Out;
			noteStart.flag = noteEnd.flag = NoteFlag::LongNote;
			noteStart.ease = edit.easeType, noteEnd.ease = EaseType::Linear;
			noteStart.guideAlpha = noteEnd.guideAlpha = 1;
			switch (edit.startType)
			{
			case EditHoldJointType::Normal:
				break;
			case EditHoldJointType::Trace:
				noteStart.flag |= NoteFlag::Trace;
				break;
			case EditHoldJointType::Hidden:
				noteStart.flag |= NoteFlag::Hidden;
				break;
			}
			switch (edit.endType)
			{
			case EditHoldJointType::Normal:
				break;
			case EditHoldJointType::Trace:
				noteEnd.flag |= NoteFlag::Trace;
				break;
			case EditHoldJointType::Hidden:
				noteEnd.flag |= NoteFlag::Hidden;
				break;
			}
			break;
		case InsertMode::InsertLongMid:
			previewNote.type = NoteType::Tick;
			previewNote.flag = setFlag(previewNote.flag, NoteFlag::Attached,
			                           edit.stepType == EditHoldStepType::Skip);
			previewNote.flag = setFlag(previewNote.flag, NoteFlag::Hidden,
			                           edit.stepType == EditHoldStepType::Hidden);
			break;
		case InsertMode::InsertFlick:
			previewNote.type = NoteType::Tap;
			previewNote.flag = NoteFlag::None;
			previewNote.flick = edit.flickType;
			break;
		case InsertMode::MakeCritical:
			previewNote.type = NoteType::Tap;
			previewNote.flag = NoteFlag::Critical;
			previewNote.flick = FlickType::None;
			break;
		case InsertMode::MakeFriction:
			previewNote.type = NoteType::Tap;
			previewNote.flag = NoteFlag::Trace;
			previewNote.flick = FlickType::None;
			break;
		case InsertMode::InsertGuide:
		{
			HoldNoteStep& holdStep = previewHold.separators.front();
			inputNoteID = !isInsertingHold ? 1 : 2;
			holdStep.flag |= HoldNoteFlag::Guide;
			previewHold.fadeType = edit.fadeType;
			holdStep.guideColor =
			    context.metadata.isExtendedScore || edit.colorType == GuideColor::Yellow
			        ? edit.colorType
			        : GuideColor::Green;
			holdStep.layer = edit.holdLayer;
			noteStart.flag = noteEnd.flag = NoteFlag::LongNote | NoteFlag::Hidden;
			noteStart.ease = edit.easeType, noteEnd.ease = EaseType::Linear;
			switch (previewHold.fadeType)
			{
			case FadeType::Classic:
				noteStart.guideAlpha = 1;
				noteEnd.guideAlpha = 0.2;
				break;
			case FadeType::Custom:
			case FadeType::Out:
				noteStart.guideAlpha = 1;
				noteEnd.guideAlpha = 0;
				break;
			case FadeType::In:
				noteStart.guideAlpha = 0;
				noteEnd.guideAlpha = 1;
				break;
			case FadeType::None:
				noteStart.guideAlpha = 1;
				noteEnd.guideAlpha = 1;
				break;
			}
			break;
		}
		case InsertMode::MakeDummy:
			previewNote.type = NoteType::Tap;
			previewNote.flag = NoteFlag::Dummy;
			previewNote.flick = FlickType::None;
			break;
		case InsertMode::InsertDamage:
			previewNote.type = NoteType::Damage;
			previewNote.flag = NoteFlag::None;
			previewNote.flick = FlickType::None;
			break;
		}

		Note& note = previewNotes.notes[inputNoteID];
		if (mouseInTimeline)
		{
			float noteWidth = edit.noteWidth;
			if (!context.metadata.isExtendedScore)
				noteWidth = std::max(std::floor(noteWidth), context.minNoteWidth());
			if (isInsertingNote)
			{
				float offset = roundToStep(mouseLane - inputLane, laneDivision);
				float snapLane = roundToStep(inputLane - noteWidth * edit.noteAlign, laneDivision) +
				                 std::min(offset + noteWidth, 0.f);
				note.width = std::clamp(std::abs(noteWidth + offset), context.minNoteWidth(),
				                        context.maxNoteWidth());
				note.lane = std::clamp(snapLane, context.minLane(), context.maxLane(note.width));
				note.tick = inputTick;
			}
			else
			{
				float snapLane = roundToStep(mouseLane - noteWidth * edit.noteAlign, laneDivision);
				note.width = noteWidth;
				note.lane = std::clamp(snapLane, context.minLane(), context.maxLane(noteWidth));
				note.tick = snapTick;
			}
		}
		note.soundEffect = edit.soundEffect;
		if (isInsertingHold && HoldNote::StepComparer()(noteEnd, noteStart) ||
		    !isInsertingHold && previewHold.steps.front() != 1)
		{
			std::swap(previewHold.steps.front(), previewHold.steps.back());
			std::swap(previewHold.joints.front(), previewHold.joints.back());
			previewHold.separators.front().ID = previewHold.steps.front();
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isInsertingNote)
		{
			switch (edit.insertMode)
			{
			case InsertMode::InsertTap:
			case InsertMode::InsertLongMid:
			case InsertMode::MakeCritical:
			case InsertMode::MakeFriction:
			case InsertMode::InsertFlick:
			case InsertMode::MakeDummy:
			case InsertMode::InsertDamage:
			{
				Note* insNote = context.insertNote(
				    previewNote, ImGui::GetIO().KeyShift ? -1 : findHoveringHoldNote());
				if (insNote && insNote->holdID >= 0 &&
				    (insNote->type == NoteType::Tick || insNote->isHidden()))
				{
					const HoldNote& hold = context.score.holdNotes.at(insNote->holdID);
					insNote->flag =
					    setFlag(insNote->flag, NoteFlag::Critical,
					            hold.holdStepAt(*insNote, context.score.notes).isCrit());
				}
				if (insNote && ImGui::GetIO().KeyCtrl)
				{
					context.selectNote(*insNote, true);
				}
			}

			break;

			case InsertMode::InsertLong:
			case InsertMode::InsertGuide:
				if (isInsertingHold)
				{
					auto&& [_, n1, n2] = context.insertHold(noteStart, noteEnd, previewHold);
					isInsertingHold = false;
					if (ImGui::GetIO().KeyCtrl)
					{
						context.selectNote(n1, false);
						context.selectNote(n2);
					}
				}
				else
				{
					setNotePosition(noteEnd, noteStart);
					isInsertingHold = true;
				}
				break;
			}

			isInsertingNote = false;
		}

		if (isInsertingNote)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

		if ((isInsertingNote || isInsertingHold) && ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			isInsertingNote = false;
			isInsertingHold = false;
		}

		if (mouseClicked)
		{
			isInsertingNote = true;
			inputTick = note.tick;
			inputLane = mouseLane;
		}
	}

	void ScoreEditorTimeline::updateScoreEvents(ImDrawList* drawList, EditArgs& edit,
	                                            PasteData& pasteData)
	{
		ImGuiIO& io = ImGui::GetIO();
		float visibleExtend = toTimeUnit(ImGui::GetFrameHeightWithSpacing());
		secs_t startTime = std::max(timelinePos.y - visibleExtend, 0.f);
		tick_t startTick = accumulateTicks(startTime, context.score.tempoChanges);
		tick_t endTick =
		    accumulateTicks(timelinePos.y + timelineSize.y, context.score.tempoChanges);
		measure_t startMeasure = accumulateMeasures(startTick, context.score.timeSignatures);
		measure_t endMeasure = accumulateMeasures(endTick, context.score.timeSignatures);
		bool previewEvent =
		    mouseInTimeline && !playing && grabbingNote < 0 && !UI::isAnyPopupOpen();
		bool eventEnabled = !playing;

		ImGuiTableFlags tableFlags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX |
		                             ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_NoClip |
		                             ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PreciseWidths;
		ImGui::SetCursorScreenPos(leftPanelScreenPos);
		if (ImGui::BeginTable("left_events", 4, tableFlags, panelScreenSize))
		{
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
			// HACK: This prevent the skill/fever column from overflowing
			// By reducing the width of the waypoint column
			float availWidth = panelScreenSize.x - ImGui::GetStyle().CellPadding.x * 7 -
			                   ImGui::GetCurrentTable()->OuterPaddingX * 2;
			auto& tableColumns = ImGui::GetCurrentTable()->Columns;
			float remWidth = std::accumulate(tableColumns.begin() + 1, tableColumns.end(),
			                                 availWidth, [](float x, const ImGuiTableColumn& col)
			                                 { return x - col.WidthRequest; });
			if (remWidth < 0 && tableColumns[1].WidthRequest + remWidth > 0)
				ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed,
				                        tableColumns[1].WidthRequest + remWidth);
			else
			{
				float contentWidth = tableColumns[1].ContentMaxXUnfrozen - tableColumns[1].WorkMinX;
				float expandableWidth =
				    tableColumns[1].WidthGiven + std::max(tableColumns[0].WidthGiven - 5, 0.f);
				float width = std::min(contentWidth, expandableWidth);
				ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, width);
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			// Update waypoints
			ImGui::TableNextColumn();
			for (auto&& [_, waypoint] : context.score.waypoints)
			{
				ImGui::PushID(waypoint.ID);
				if (waypointControl(drawList, waypoint, eventEnabled))
					openEvent(waypoint);
				ImGui::PopID();
			}

			// Update fever
			ImGui::TableNextColumn();
			ImGui::Dummy({ UI::scale(52), 1 }); // Padding
			if (feverControl(drawList, context.score.fever, eventEnabled))
				openEvent(context.score.fever);

			// Update skill triggers
			ImGui::TableNextColumn();
			ImGui::Dummy({ UI::scale(30), 1 }); // Padding
			for (auto&& skill : context.score.skills)
				if (skillControl(drawList, skill, eventEnabled))
					openEvent(skill);

			ImGui::EndTable();
		}

		ImGui::SetCursorScreenPos(rightPanelScreenPos);
		if (ImGui::BeginTable("right_events", 4, tableFlags, panelScreenSize))
		{
			ImGui::TableNextRow();

			// Update hi-speed changes
			// Render the other layer first
			ImGui::TableSetColumnIndex(3);
			hispeedExPanelStartX = ImGui::GetCursorScreenPos().x;
			for (size_t l = 0; l < context.score.layers.size(); ++l)
			{
				if (!context.isLayerVisible(l) || context.isLayerSelected(l))
					continue;
				const auto& hispeedChanges = context.score.layers[l].hiSpeedChanges;
				auto hispdIt = std::reverse_iterator(hispeedChanges.upper_bound(endTick));
				auto hispdEnd = std::reverse_iterator(hispeedChanges.lower_bound(startTick));
				for (; hispdIt != hispdEnd; ++hispdIt)
				{
					const HiSpeed& hispeed = hispdIt->second;
					hiSpeedControl(drawList, hispeed, context.selection.has(hispeed), false);
				}
			}
			// Then render the selected layer
			ImGui::TableSetColumnIndex(2);
			hispeedPanelStartX = ImGui::GetCursorScreenPos().x;
			ImGui::Dummy({ UI::scale(25), 1 }); // Padding in case the column is empty
			const auto& hispeedChanges = context.score.layers[context.selectedLayer].hiSpeedChanges;
			auto hispdIt = std::reverse_iterator(hispeedChanges.upper_bound(endTick));
			auto hispdEnd = std::reverse_iterator(hispeedChanges.lower_bound(startTick));
			auto hispdMouse = hispeedChanges.rend();
			if (ImGui::GetCurrentTable()->HoveredColumnBody == 2)
				hispdMouse = std::reverse_iterator(hispeedChanges.lower_bound(mouseTick));
			for (; hispdIt != hispdEnd; ++hispdIt)
			{
				if (hispdIt == hispdMouse)
					continue;
				const HiSpeed& hispeed = hispdIt->second;
				if (hiSpeedControl(drawList, hispeed, context.selection.has(hispeed), eventEnabled))
					openEvent(hispeed);
			}

			if (hispdMouse != hispeedChanges.rend() &&
			    hiSpeedControl(drawList, hispdMouse->second,
			                   context.selection.has(hispdMouse->second), eventEnabled))
			{
				if (io.KeyCtrl)
					context.selectHiSpeed(hispdMouse->second);
				else if (io.KeyAlt)
					context.deselectHiSpeed(hispdMouse->second);
				else
					openEvent(hispdMouse->second);
			}
			if (previewEvent && edit.insertMode == InsertMode::InsertHiSpeed)
			{
				ImGui::TableSetColumnIndex(2);
				ImGui::PushID("preview");
				hiSpeedControl(drawList, getPreviewHispeed(edit), false, false);
				ImGui::PopID();
			}
			if (pasteData.pasting && !(isInsertingNote || isInsertingHold) && mouseInTimeline &&
			    !playing)
			{
				ImGui::TableSetColumnIndex(2);
				ImGui::PushID("paste");
				for (const auto& [_, hs] : pasteData.hiSpeedChanges)
				{
					HiSpeed hispeed = hs;
					hispeed.layer = -1;
					hispeed.tick += snapTick;
					hiSpeedControl(drawList, hispeed, false, false);
				}
				ImGui::PopID();
			}

			// Update time signature changes
			ImGui::TableSetColumnIndex(1);
			ImGui::Dummy({ UI::scale(25), 1 }); // Padding in case the column is empty
			auto tsIt = context.score.timeSignatures.lower_bound(startMeasure);
			auto tsEnd = context.score.timeSignatures.upper_bound(endMeasure);
			for (; tsIt != tsEnd; ++tsIt)
			{
				auto&& [_, ts] = *tsIt;
				if (timeSignatureControl(drawList, ts, eventEnabled))
					openEvent(ts);
			}
			if (previewEvent && edit.insertMode == InsertMode::InsertTimeSign)
			{
				ImGui::PushID("preview");
				timeSignatureControl(drawList, getPreviewTimeSignatrue(edit), false);
				ImGui::PopID();
			}

			// Update bpm changes
			ImGui::TableSetColumnIndex(0);
			ImGui::Dummy({ UI::scale(50), 1 }); // Padding in case the column is empty
			auto tempoIt = std::reverse_iterator(context.score.tempoChanges.upper_bound(endTick));
			auto tempoEnd =
			    std::reverse_iterator(context.score.tempoChanges.lower_bound(startTick));
			auto tempoMouse = context.score.tempoChanges.rend();
			if (ImGui::GetCurrentTable()->HoveredColumnBody == 0)
				tempoMouse =
				    std::reverse_iterator(context.score.tempoChanges.lower_bound(mouseTick));
			for (; tempoIt != tempoEnd; ++tempoIt)
			{
				if (tempoIt == tempoMouse)
					continue;
				auto&& [_, tempo] = *tempoIt;
				if (bpmControl(drawList, tempo, eventEnabled))
					openEvent(tempo);
			}
			if (tempoMouse != context.score.tempoChanges.rend() &&
			    bpmControl(drawList, tempoMouse->second, eventEnabled))
				openEvent(tempoMouse->second);
			if (previewEvent && edit.insertMode == InsertMode::InsertBPM)
			{
				ImGui::PushID("preview");
				bpmControl(drawList, getPreviewTempo(edit), false);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isInsertingEvent)
		{
			switch (edit.insertMode)
			{
			case InsertMode::InsertBPM:
				context.insertTempoChange(getPreviewTempo(edit));
				break;
			case InsertMode::InsertTimeSign:
				context.insertTimeSignature(getPreviewTimeSignatrue(edit));
				break;
			case InsertMode::InsertHiSpeed:
				context.insertHispeedChange(getPreviewHispeed(edit));
				break;
			}
			isInsertingEvent = false;
		}
		if (mouseClicked)
			isInsertingEvent = true;

		// Update event editor
		eventEditor();
	}

	void ScoreEditorTimeline::debug()
	{
		auto timelineName = ImGui::GetCurrentWindowRead()->Name;
		ImGui::Begin(DebugWindow::getWindowName(), NULL, ImGuiWindowFlags_NoResize);
		ImGui::Spacing();
		if (ImGui::TreeNodeEx(timelineName, ImGuiTreeNodeFlags_Framed))
		{
			ImVec2 absScreenPos = ImGui::GetWindowPos();
			ImVec2 absScreenSize = ImGui::GetWindowSize();
			ImGui::Text("Window position: (%.2f, %.2f)\nWindow size: (%.2f, %.2f)", absScreenPos.x,
			            absScreenPos.y, absScreenSize.x, absScreenSize.y);
			ImGui::Text(
			    "Timeline position: (%.2f, %.2f)\nTimeline size: (%.2f, %.2f)\nLaneWidth: %.2f",
			    timelineScreenPos.x, timelineScreenPos.y, timelineScreenSize.x,
			    timelineScreenSize.y, toScreenWidth(1));
			ImGui::Text("Panel size: (%.2f, %.2f)", panelScreenSize.x, panelScreenSize.y);
			ImGui::Separator();

			if (ImGui::GetIO().WantCaptureMouse)
			{
				ImVec2 absMousePos = ImGui::GetMousePos();
				ImGui::Text("Mouse abs position: (%.2f, %.2f)", absMousePos.x, absMousePos.y);
				if (mouseInTimeline)
				{
					ImGui::Text("Mouse timeline pos: (lane %.2f, sec %.2f)",
					            toLanePos(absMousePos.x), toTimePos(absMousePos.y));
					ImGui::Text("Mouse Pos: (lane %.2f, tick %d | tick %d)", mouseLane, mouseTick,
					            snapTick);
				}
				else
				{
					ImGui::TextDisabled("Mouse is not in timeline!");
					ImGui::TextDisabled("Last Mouse Pos: (lane %.2f, tick %d)", mouseLane,
					                    mouseTick);
				}
			}
			else
			{
				ImGui::TextDisabled("Mouse position is invalid");
				ImGui::TextDisabled("Mouse is not in timeline!");
				ImGui::Text("Last Mouse Pos: (lane %.2f, tick %d)", mouseLane, mouseTick);
			}
			ImGui::Separator();
			ImGui::Text("Current offset (lane %.2f, %.2f secs)", targetOffset.x, targetOffset.y);
			ImGui::Text("Visual offset (lane %.2f, %.2f secs)", visualOffset.x, visualOffset.y);
			ImGui::Text("Max offset: %.2f secs", maxTime);
			ImGui::Text("Zoom X: %.4f", zoomX);
			ImGui::Text("Zoom Y: %.4f", zoomY);
			ImGui::Text("Hover Timer: %.2f", selectHoverTimer);
			id_t curNoteID =
			    context.hoveringNotes.size() ? context.hoveringNotes.back()->ID : grabbingNote;
			if (curNoteID >= 0)
			{
				ImGui::Separator();
				const Note& note = context.score.notes.at(curNoteID);
				constexpr const char* NOTE_TYPE_STR[] = { "TAP", "TICK", "DAMAGE" };
				static_assert(std::size(NOTE_TYPE_STR) == size_t(NoteType::NoteTypeCount));
				ImGui::Text("Note:\n-ID: %d\n-Type: %s\n-Layer: %d\n-Tick: %d\n"
				            "-Lane: %.2f\n-Width: %.2f\n-Flick: %s",
				            note.ID, NOTE_TYPE_STR[(int)note.type], note.layer, note.tick,
				            note.lane, note.width,
				            (const char*)localize(flickTypeTexts[(int)note.flick]));
				if ((int)note.flag)
				{
					ImGui::Text("-Flags:");
					if (hasFlag(note.flag, NoteFlag::Critical))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1, 1, 0, 1), "| Crit");
					}
					if (hasFlag(note.flag, NoteFlag::Trace))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0, 1, 0, 1), "| Trace");
					}
					if (hasFlag(note.flag, NoteFlag::Dummy))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1, 0, 0, 1), "| Fake");
					}
					if (hasFlag(note.flag, NoteFlag::Attached))
					{
						ImGui::SameLine();
						ImGui::Text("| Attached");
					}
					if (hasFlag(note.flag, NoteFlag::Hidden))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0.8, 1, 1, 1), "| Hidden");
					}
					if (hasFlag(note.flag, NoteFlag::LongNote))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1, 0.8, 1, 0.8), "| Long");
					}
					if (hasFlag(note.flag, NoteFlag::NonAttached))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1, 0.8, 0.8, 0.8), "| NonAttached");
					}
				}
				else
					ImGui::TextDisabled("-Flags: None");

				if (note.isHold())
				{
					ImGui::Separator();
					const HoldNote& hold = context.score.holdNotes.at(note.holdID);
					ImGui::Text("Hold: %d\n-Steps: %zd\n-Joints: %zd\n-Seperators: %zd", hold.ID,
					            hold.steps.size(), hold.joints.size(), hold.separators.size());

					for (size_t i = 0; i < hold.separators.size(); ++i)
					{
						const HoldNoteStep& holdStep = hold.separators[i];
						ImGui::SeparatorText(IO::formatString("[%zd]", i).c_str());
						ImGui::Text("-ID: %d", i, holdStep.ID);
						if ((int)holdStep.flag)
						{
							ImGui::Text("-Flags:");
							if (hasFlag(holdStep.flag, HoldNoteFlag::Critical))
							{
								ImGui::SameLine();
								ImGui::TextColored(ImVec4(1, 1, 0, 1), "| Crit");
							}
							if (hasFlag(holdStep.flag, HoldNoteFlag::Dummy))
							{
								ImGui::SameLine();
								ImGui::TextColored(ImVec4(1, 0, 0, 1), "| Dummy");
							}
							if (hasFlag(holdStep.flag, HoldNoteFlag::Guide))
							{
								ImGui::SameLine();
								ImGui::Text("| Guide");
							}
						}
						else
							ImGui::Text("-Flags: None");

						ImGui::Text(
						    "-Guide Color: %s\n-Alpha: %.2f\n-Layer: %s",
						    (const char*)localize(guideColorAllTexts[(int)holdStep.guideColor]),
						    note.guideAlpha,
						    (const char*)localize(holdLayerTexts[(int)holdStep.layer]));
					}

					ImGui::Text("-Fade Type: %s",
					            (const char*)localize(fadeTypeTexts[(int)hold.fadeType]));
					if (!note.isAttached())
						ImGui::Text("-Ease: %s",
						            (const char*)localize(easeTypeTexts[(int)note.ease]));
				}
				else
				{
					ImGui::Separator();
					ImGui::Text("Hold: --\n-Steps: --\n-Joints: --\n-Prev: --\n-Next: __\n");
					ImGui::Text("-Flags: --");
					ImGui::Text("-No Guide!\n-No Alpha!\n-No Layer\n-No Fade!");
				}
			}
			else
			{
				ImGui::Separator();
				ImGui::Text("Note:\n-ID: --\n-Type: --\n-Layer: --\n-Tick: --\n-Lane: --\n-Width: "
				            "--\n-Flick: --");
				ImGui::Text("-Flags: --");
				ImGui::Text("Hold: --\n-Steps: --\n-Joints: --\n-Prev: --\n-Next: __\n");
				ImGui::Text("-Flags: --");
				ImGui::Text("-No Guide!\n-No Fade!\n-No Alpha!");
			}
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Music", ImGuiTreeNodeFlags_DefaultOpen))
			{
				UI::beginPropertyTable();
				UI::labelPropertyRow("Music Initialized",
				                     boolToString(context.audio->isMusicInitialized()));
				UI::labelPropertyRow("Music Filename", context.audio->musicBuffer.name);

				float musicTime,
				    musicTimeMs = std::modf(context.audio->getMusicPosition(), &musicTime);
				auto&& [musicTimeMin, musicTimeSec] = integerDivide<int>(musicTime, 60);
				float musicTotal,
				    musicTotalMs = std::modf(context.audio->getMusicLength(), &musicTotal);
				auto&& [musicTotalMin, musicTotalSec] = integerDivide<int>(musicTotal, 60);
				UI::labelPropertyRow(
				    "Music Time", IO::formatString("%02d:%02d.%02.f/%02d:%02d.%02.f", musicTimeMin,
				                                   musicTimeSec, musicTimeMs * 100, musicTotalMin,
				                                   musicTotalSec, musicTotalMs * 100));
				UI::labelPropertyRow("Sample Rate",
				                     std::to_string(context.audio->musicBuffer.sampleRate));
				UI::labelPropertyRow(
				    "Effective Sample Rate",
				    std::to_string(context.audio->musicBuffer.effectiveSampleRate));
				UI::labelPropertyRow("Channel Count",
				                     std::to_string(context.audio->musicBuffer.channelCount));
				UI::endPropertyTable();
			}
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Waveform", ImGuiTreeNodeFlags_DefaultOpen) &&
			    ImGui::BeginTabBar("waveform_tabbar"))
			{
				auto showWaveformTab = [&](const char* label, const Audio::WaveformMipChain& wav)
				{
					if (!ImGui::BeginTabItem(label))
						return;
					UI::beginPropertyTable();
					UI::labelPropertyRow("Mip Count", std::to_string(wav.getUsedMipCount()));
					if (wav.isEmpty() || context.isMusicLoading->load())
					{
						UI::labelPropertyRow("Estimated Memory Usage", "0 bytes");
						UI::labelPropertyRow("Mip Sample Count", "0");
						UI::labelPropertyRow("Mip Sample Rate", "0");
						UI::labelPropertyRow("Mip Second Per Sample", "0");
						UI::labelPropertyRow("Mip Amplitude At Mouse", "0");
					}
					else
					{
						size_t mem = std::accumulate(
						    wav.mips.begin(), wav.mips.end(), size_t(0),
						    [](size_t i, const Audio::WaveformMip& mip)
						    { return i + mip.absoluteSamples.size() * sizeof(int16_t); });
						UI::labelPropertyRow("Estimated Memory Usage",
						                     IO::formatString("%zd kb", mem / 1000));
						double invSample = timelineSize.y / timelineScreenSize.y;
						auto& mip = wav.findClosestMip(invSample);
						UI::labelPropertyRow("Mip Sample Count", std::to_string(mip.sampleCount));
						UI::labelPropertyRow("Mip Sample Rate",
						                     IO::formatString("%g", mip.samplesPerSecond));
						UI::labelPropertyRow("Mip Second Per Sample",
						                     IO::formatString("%g", mip.secondsPerSample));
						secs_t mouseTime =
						    ImGui::IsMousePosValid() ? toTimePos(ImGui::GetMousePos().y) : 0.f;
						float amp = wav.getAmplitudeAt(mip, mouseTime, invSample);
						UI::labelPropertyRow("Mip Amplitude At Mouse", IO::formatString("%f", amp));
					}
					UI::endPropertyTable();
					ImGui::EndTabItem();
				};
				showWaveformTab("Waveform L", context.waveformL);
				showWaveformTab("Waveform R", context.waveformR);
				if (ImGui::Button("Re-Generate Waveform", { -1, UI::btnSmall.y }))
				{
					context.waveformL.generateMipChainsFromSampleBuffer(context.audio->musicBuffer,
					                                                    0);
					context.waveformR.generateMipChainsFromSampleBuffer(context.audio->musicBuffer,
					                                                    1);
				}
				ImGui::EndTabBar();
			}
			ImGui::TreePop();
		}
		ImGui::End();
	}

	void ScoreEditorTimeline::eventEditor()
	{
		if (shouldOpenEventEditor)
		{
			ImGui::OpenPopup("edit_event");
			shouldOpenEventEditor = false;
		}

		ImGui::SetNextWindowSize({ 280, -1 }, ImGuiCond_Always);
		if (ImGui::BeginPopup("edit_event"))
		{
			if (Tempo* tempo = std::get_if<Tempo>(&eventEditArgs))
			{
				ImGui::TextUnformatted(localize(Text::editBpm));
				ImGui::Separator();
				UI::beginPropertyTable();
				UI::floatPropertyRow(Text::bpm, tempo->quarterPerMinute, "%g");
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					auto tempoIt = context.score.tempoChanges.find(tempo->tick);
					if (tempoIt == context.score.tempoChanges.end())
					{
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return;
					}
					const Tempo before = tempoIt->second;
					tempo->quarterPerMinute = tempoIt->second.quarterPerMinute =
					    std::clamp(tempo->quarterPerMinute, MIN_BPM, MAX_BPM);
					if (before.quarterPerMinute != tempoIt->second.quarterPerMinute)
						context.pushEdit("Change tempo",
						                 ChangeTempo{ tempo->tick, before, tempoIt->second });
				}
				UI::endPropertyTable();
				ImGui::Separator();
				ImGui::BeginDisabled(tempo->tick == 0);
				if (ImGui::Button(localize(Text::remove), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					auto tempoIt = context.score.tempoChanges.find(tempo->tick);
					if (tempoIt != context.score.tempoChanges.end())
					{
						const Tempo before = tempoIt->second;
						context.score.tempoChanges.erase(tempoIt);
						context.pushEdit("Remove tempo change",
						                 ChangeTempo{ tempo->tick, before, std::nullopt });
					}
				}
				ImGui::EndDisabled();
			}
			else if (TimeSignature* timeSig = std::get_if<TimeSignature>(&eventEditArgs))
			{
				ImGui::TextUnformatted(localize(Text::editTimeSignature));
				ImGui::Separator();
				UI::beginPropertyTable();
				if (UI::timeSignatureSelect(timeSig->numerator, timeSig->denominator))
				{
					auto tsIt = context.score.timeSignatures.find(timeSig->measure);
					if (tsIt == context.score.timeSignatures.end())
					{
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return;
					}
					TimeSignature& ts = tsIt->second;
					const TimeSignature before = ts;
					ts.numerator = std::clamp(timeSig->numerator, MIN_TIME_SIGNATURE,
					                          MAX_TIME_SIGNATURE_NUMERATOR);
					ts.denominator = std::clamp(timeSig->denominator, MIN_TIME_SIGNATURE,
					                            MAX_TIME_SIGNATURE_DENOMINATOR);

					if (before.numerator != ts.numerator || before.denominator != ts.denominator)
						context.pushEdit("Change time signature",
						                 ChangeTimeSignature{ timeSig->measure, before, ts });
				}
				UI::endPropertyTable();

				// cannot remove the first time signature
				ImGui::BeginDisabled(timeSig->measure == 0);
				ImGui::Separator();
				if (ImGui::Button(localize(Text::remove), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					auto tsIt = context.score.timeSignatures.find(timeSig->measure);
					if (tsIt != context.score.timeSignatures.end())
					{
						const TimeSignature before = tsIt->second;
						context.score.timeSignatures.erase(tsIt);
						context.pushEdit(
						    "Remove time signature",
						    ChangeTimeSignature{ timeSig->measure, before, std::nullopt });
					}
				}
				ImGui::EndDisabled();
			}
			else if (HiSpeed* hispeed = std::get_if<HiSpeed>(&eventEditArgs))
			{
				ImGui::TextUnformatted(localize(Text::editHiSpeed));
				ImGui::Separator();
				bool eventEdited = false;
				UI::beginPropertyTable();
				UI::floatPropertyRow(Text::hiSpeedSpeed, hispeed->speed, "%g");
				eventEdited |= ImGui::IsItemDeactivatedAfterEdit();
				if (context.metadata.isExtendedScore)
				{
					const char* beatFmtStr = localizeOrInsert(
					    "__fmt_beat_float",
					    []() { return IO::formatString("%%.3f %s", localize(Text::beat)); });
					eventEdited |= UI::selectPropertyRow(Text::hiSpeedEase, hispeed->ease,
					                                     hispeedEaseTypeTexts);
					UI::floatPropertyRow(Text::hiSpeedSkipBeat, hispeed->skips, beatFmtStr);
					eventEdited |= ImGui::IsItemDeactivatedAfterEdit();
					UI::checkboxPropertyRow(Text::hiSpeedHideNotes, hispeed->hideNotes);
					eventEdited |= ImGui::IsItemDeactivatedAfterEdit();
				}
				if (eventEdited)
				{
					if (!isArrayIndexInBounds(hispeed->layer, context.score.layers))
					{
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return;
					}
					auto& hispeedChanges = context.score.layers[hispeed->layer].hiSpeedChanges;
					auto hispeedIt = hispeedChanges.find(hispeed->tick);
					if (hispeedIt == hispeedChanges.end())
					{
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return;
					}
					auto& hspd = hispeedIt->second;
					const HiSpeed before = hspd;
					hspd.speed = std::clamp(hispeed->speed, -MAX_HISPEED, MAX_HISPEED);
					hspd.skips = std::clamp(hispeed->skips, -MAX_HISPEED, MAX_HISPEED);
					hspd.ease = hispeed->ease;
					hspd.hideNotes = hispeed->hideNotes;
					if (!isSame(before, hspd))
						context.pushEdit("Change hi-speed",
						                 ChangeHiSpeed{ { hspd.layer, hspd.tick }, before, hspd });
					context.updateSelectionFlag();
				}
				UI::endPropertyTable();

				ImGui::Separator();
				if (ImGui::Button(localize(Text::remove), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					auto& hispeedChanges = context.score.layers[hispeed->layer].hiSpeedChanges;
					auto hispeedIt = hispeedChanges.find(hispeed->tick);
					if (hispeedIt != hispeedChanges.end())
					{
						const HiSpeed before = hispeedIt->second;
						// The selection loses the hi-speed. Undo selects it again
						SelectionRef selectionBefore = context.getSelectionSnapshot();
						context.deselectHiSpeed(before);
						hispeedChanges.erase(hispeedIt);
						context.pushHistory(
						    "Remove hi-speed change",
						    ChangeHiSpeed{ { before.layer, before.tick }, before, std::nullopt },
						    SelectionChange{ selectionBefore, context.getSelectionSnapshot() });
						context.updateSelectionFlag();
					}
				}
			}
			else if (Waypoint* waypoint = std::get_if<Waypoint>(&eventEditArgs))
			{
				if (ImGui::IsWindowAppearing() && !mouseInTimeline)
				{
					secs_t time = accumulateDuration(waypoint->tick, context.score.tempoChanges);
					float posX =
					    leftPanelScreenPos.x + panelScreenSize.x - ImGui::GetWindowSize().x;
					float posY = screenCenterY() + toScreenHeight(ORIGIN_Y + targetOffset.y - time);
					ImGui::SetWindowPos(ImGui::GetCurrentWindowRead(), { posX, posY });
				}

				ImGui::TextUnformatted(localize(Text::editHiSpeed));
				ImGui::Separator();

				UI::beginPropertyTable();
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				UI::stringPropertyRow(Text::waypointName, waypoint->name);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					auto it = context.score.waypoints.find(waypoint->ID);
					if (it == context.score.waypoints.end())
					{
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return;
					}
					auto&& [_, wp] = *it;
					const Waypoint before = wp;
					wp.name = waypoint->name;
					if (before.name != wp.name)
						context.pushEdit("Change waypoint", ChangeWaypoint{ wp.ID, before, wp });
				}
				UI::endPropertyTable();

				ImGui::Separator();
				if (ImGui::Button(localize(Text::remove), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					context.eraseWaypoint(waypoint->ID);
				}
			}
			else if (Fever* fever = std::get_if<Fever>(&eventEditArgs))
			{
				ImGui::TextUnformatted(localize(Text::editFever));
				ImGui::Separator();
				if (ImGui::Button(localize(Text::remove), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					const Fever before = context.score.fever;
					context.score.fever.startTick = -1;
					context.score.fever.endTick = -1;
					context.pushEdit("Remove FEVER event",
					                 ChangeFever{ { before, context.score.fever } });
				}
			}
			else if (Skill* skill = std::get_if<Skill>(&eventEditArgs))
			{
				ImGui::TextUnformatted(localize(Text::editSkill));
				ImGui::Separator();
				if (context.metadata.isExtendedScore)
				{
					bool eventEdited = false;
					UI::beginPropertyTable();
					eventEdited |=
					    UI::selectPropertyRow(Text::skillEffect, skill->effect, skillEffectTexts);
					int level = skill->level;
					if (UI::intPropertyRow(Text::skillLevel, level, "Lv.%d", 1, 4))
					{
						skill->level = level;
						eventEdited |= true;
					}
					UI::endPropertyTable();
					ImGui::Separator();
					if (eventEdited)
					{
						auto node = context.score.skills.extract(*skill);
						if (!node.empty())
						{
							Skill& sk = node.value();
							const Skill before = sk;
							sk.effect = skill->effect;
							sk.level = skill->level;
							const Skill after = sk;
							context.score.skills.insert(std::move(node));
							if (before.effect != after.effect || before.level != after.level)
								context.pushEdit("Edit skill trigger",
								                 ChangeSkill{ after.tick, before, after });
						}
					}
				}
				if (ImGui::Button(localize(Text::remove), ImVec2(-1, UI::btnSmall.y + 2)))
				{
					ImGui::CloseCurrentPopup();
					auto skillIt = context.score.skills.find(*skill);
					if (skillIt != context.score.skills.end())
					{
						const Skill before = *skillIt;
						context.score.skills.erase(skillIt);
						context.pushEdit("Remove skill trigger",
						                 ChangeSkill{ before.tick, before, std::nullopt });
					}
				}
			}
			else
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	secs_t ScoreEditorTimeline::getCurrentTime() const { return curTime; }

	tick_t ScoreEditorTimeline::getCurrentTick() const
	{
		return accumulateTicks(curTime, context.score.tempoChanges);
	}

	measure_t ScoreEditorTimeline::getCurrentMeasure() const
	{
		return accumulateMeasures(getCurrentTick(), context.score.timeSignatures);
	}

	const Jacket& ScoreEditorTimeline::getJacket() const { return jacket; }

	Background& ScoreEditorTimeline::getBackground() { return background; }

	ImVec2 ScoreEditorTimeline::screenCenter() const noexcept
	{
		return { screenCenterX(), screenCenterY() };
	}

	float ScoreEditorTimeline::screenCenterX() const noexcept
	{
		return timelineScreenPos.x + timelineScreenSize.x / 2;
	}

	float ScoreEditorTimeline::screenCenterY() const noexcept
	{
		return timelineScreenPos.y + timelineScreenSize.y / 2;
	}

	ImVec2 ScoreEditorTimeline::toScreen(ImVec2 pos) const noexcept
	{
		return ImVec2(toScreenPosX(pos.x), toScreenPosY(pos.y));
	}

	float ScoreEditorTimeline::toScreenPosX(float lane) const noexcept
	{
		return screenCenterX() - toScreenWidth(ORIGIN_X + visualOffset.x - lane);
	}

	float ScoreEditorTimeline::toScreenPosY(secs_t secs) const noexcept
	{
		return screenCenterY() + toScreenHeight(ORIGIN_Y + visualOffset.y - secs);
	}

	ImVec2 ScoreEditorTimeline::fromScreen(ImVec2 screenPos) const noexcept
	{
		return ImVec2(toLanePos(screenPos.x), toTimePos(screenPos.y));
	}

	float ScoreEditorTimeline::toLanePos(float screenX) const noexcept
	{

		return toLaneUnit(screenX - screenCenterX()) + ORIGIN_X + visualOffset.x;
	}

	secs_t ScoreEditorTimeline::toTimePos(float screenY) const noexcept
	{
		return toTimeUnit(screenCenterY() - screenY) + ORIGIN_Y + visualOffset.y;
	}

	float ScoreEditorTimeline::toScreenWidth(float lanes) const noexcept
	{
		return (lanes * timelineScreenSize.x) / (UNIT_X / zoomX);
	}

	float ScoreEditorTimeline::toScreenHeight(secs_t secs) const noexcept
	{
		return (secs * timelineScreenSize.y) / (UNIT_Y / zoomY);
	}

	float ScoreEditorTimeline::toLaneUnit(float width) const noexcept
	{
		return (width / timelineScreenSize.x) * (UNIT_X / zoomX);
	}

	secs_t ScoreEditorTimeline::toTimeUnit(float height) const noexcept
	{
		return (height / timelineScreenSize.y) * (UNIT_Y / zoomY);
	}

	void ScoreEditorTimeline::loadMusic(const std::string& filename)
	{
		context.isMusicLoading->store(true);
		setPlaying(false);
		Result result = context.audio->loadMusic(filename);
		if (!result.isOk() && filename.size())
		{
			std::string errorMessage = IO::formatString(
			    "%s\n%s: %s\n%s: %s", localize(Text::errorLoadMusicFile), localize(Text::musicFile),
			    filename, localize(Text::error), result.getMessage());

			IO::messageBox(APP_NAME, errorMessage, IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Error,
			               Application::getInstance().getAppWindowHandle());
		}
		else
		{
			context.metadata.musicFile = filename;
			context.isPendingChangeMusicOffset = true;
		}

		context.waveformL.generateMipChainsFromSampleBuffer(context.audio->musicBuffer, 0);
		context.waveformR.generateMipChainsFromSampleBuffer(context.audio->musicBuffer, 1);

		context.isMusicLoading->store(false);
	}
	void ScoreEditorTimeline::loadScore(Score score, ScoreMetadata metadata,
	                                    const std::string& filename)
	{
		context.deselectAll();
		context.selectedLayer = 0;
		context.history.clear();
		context.recentHistoryUndo = context.history.undoCount();
		context.score = std::move(score);
		context.metadata = context.workingMetadata = std::move(metadata);
		context.upToDate = true;
		context.updateViews();
		context.scoreStats.calculateStats(context.score);
		context.nextNoteID = std::accumulate(context.score.notes.begin(), context.score.notes.end(),
		                                     0, [](id_t id, const std::pair<id_t, Note>& v)
		                                     { return std::max(id, v.first + 1); });
		context.nextHoldID = std::accumulate(
		    context.score.holdNotes.begin(), context.score.holdNotes.end(), 0,
		    [](id_t id, const std::pair<id_t, HoldNote>& v) { return std::max(id, v.first + 1); });
		if (ScoreSerializeController::toSerializeFormat(filename) == SerializeFormat::NativeFormat)
		{
			context.filename = filename;
			setTimelineName(
			    IO::toString(IO::File::getFilenameWithoutExtension(IO::stringToPath(filename))));
		}
		else
		{
			context.filename.clear();
			setTimelineName(windowUntitled);
		}

		context.isPendingLoadMusic = true;

		tick_t maxTick =
		    context.notesOrderedView.size() ? context.notesOrderedView.rbegin()->second->tick : 0;
		for (auto&& layer : context.score.layers)
			if (layer.hiSpeedChanges.size())
				maxTick = std::max(layer.hiSpeedChanges.rbegin()->second.tick, maxTick);

		// Current offset maybe greater than calculated offset from score
		maxTime = std::max(maxTime, accumulateDuration(maxTick, context.score.tempoChanges));
	}

	bool ScoreEditorTimeline::saveScore(const std::string& filename)
	{
		try
		{
			NativeScoreSerializer().serialize({ context.score, context.metadata }, filename);

			setTimelineName(
			    IO::toString(IO::File::getFilenameWithoutExtension(IO::stringToPath(filename))));
			context.recentHistoryUndo = context.history.undoCount();
			context.upToDate = true;
		}
		catch (const std::exception& err)
		{
			IO::messageBox(APP_NAME,
			               IO::formatString("%s\n%s: %s", localize(Text::errorSaveScoreFile),
			                                localize(Text::error), err.what()),
			               IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
			return false;
		}
		return true;
	}
} // namespace MikuMikuWorld

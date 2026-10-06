#include "Application.h"
#include "ApplicationConfiguration.h"
#include "ScoreEditorWindows.h"
#include "ScoreEditor.h"
#include "Constants.h"
#include "File.h"
#include "NoteTypes.h"
#include "ScoreContext.h"
#include "UI.h"
#include "Text.h"
#include "Colors.h"
#include "Utilities.h"

namespace MikuMikuWorld
{
	// Get the label of the shortcut and automatic update if the shortcut changed
	static const char* bindingLabel(std::string_view txt, std::string_view id, InputBinding& input,
	                                const MultiInputBinding& shortcuts)
	{
		auto fetchLabel = [](std::string_view txt, const MultiInputBinding& shortcuts)
		{
			std::string label = localize(txt).string;
			const char* shortcut = ToShortcutString(shortcuts);
			if (shortcut && strlen(shortcut))
				label.append(" (").append(shortcut).append(")");
			return label;
		};

		if ((shortcuts.count > 0 && shortcuts.bindings[0] == input) ||
		    (shortcuts.count == 0 && input.keyCode == 0))
			return localizeOrInsert(id, fetchLabel, txt, shortcuts);
		else if (shortcuts.count > 0)
		{
			input = shortcuts.bindings[0];
			return getLocalization().updateText(id, { fetchLabel(txt, shortcuts) })->second;
		}
		else if (shortcuts.count == 0)
		{
			input.keyCode = ImGuiKey_None;
			input.keyModifiers = ImGuiKey_None;
			return getLocalization().updateText(id, { fetchLabel(txt, shortcuts) })->second;
		}
		else
			return localize(txt);
	}

	bool EditorToolbar::iconButton(const char* icon, std::string_view shortcutId,
	                               InputBinding& input, const MultiInputBinding& shortcuts,
	                               bool enabled, bool selected)
	{
		bool activated = false;
		if (enabled && !ImGui::GetIO().WantCaptureKeyboard)
			// Don't active, just dectect that the shortcut is active for visual
			selected |= ImGui::IsAnyDown(shortcuts);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, UI::scale({ 2, 2 }));
		ImGui::BeginDisabled(!enabled);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		if (selected)
		{
			auto& styleColors = ImGui::GetStyle().Colors;
			ImGui::PushStyleColor(ImGuiCol_Button, styleColors[ImGuiCol_ButtonActive]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, styleColors[ImGuiCol_ButtonActive]);
		}

		activated |= ImGui::Button(icon, UI::toolbarBtnSize);
		ImGui::SameLine();

		UI::tooltip(bindingLabel(shortcuts.name, shortcutId, input, shortcuts));

		ImGui::PopStyleColor(selected ? 3 : 1);
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		return activated;
	}

	bool EditorToolbar::imageButton(const Texture* texture, const Sprite* sprite,
	                                std::string_view txt, std::string_view shortcutId,
	                                InputBinding& input, const MultiInputBinding& shortcuts,
	                                bool enabled, bool selected)
	{
		bool activated = false;
		if (!texture || !sprite)
		{
			ImGui::PushID(shortcutId.data(), shortcutId.data() + shortcutId.size());
			// Fallback to regular button
			activated = iconButton("?", shortcutId, input, shortcuts, enabled, selected);
			ImGui::PopID();
			return activated;
		}
		ImGui::BeginDisabled(!enabled);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 1, 1 });
		if (selected)
		{
			auto& styleColors = ImGui::GetStyle().Colors;
			ImGui::PushStyleColor(ImGuiCol_Button, styleColors[ImGuiCol_TabSelected]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, styleColors[ImGuiCol_TabSelected]);
		}
		auto&& [uv0, uv1] = texture->getCoords(*sprite);
		activated |= ImGui::ImageButton(sprite->getName().c_str(), texture->getID(),
		                                UI::toolbarBtnImgSize, uv0, uv1);
		ImGui::SameLine();

		UI::tooltip(bindingLabel(txt, shortcutId, input, shortcuts));

		if (selected)
			ImGui::PopStyleColor(3);
		else
			ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		ImGui::EndDisabled();

		return activated;
	}

	void EditorToolbar::update(ScoreEditorState& state, ScoreContext* context, EditArgs& edit,
	                           PasteData& pasteData)
	{
		auto& input = getConfig().input;
		ImGuiViewport* viewport = ImGui::GetMainViewport();

		// keep toolbar on top in main viewport
		ImGui::SetNextWindowSizeConstraints({ viewport->WorkSize.x, 0 }, { FLT_MAX, FLT_MAX });
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);

		// toolbar style
		ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVarY(ImGuiStyleVar_WindowPadding, UI::scale(4));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

		constexpr ImGuiWindowFlags ImGuiWindowFlags_Toolbar =
		    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
		    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs |
		    ImGuiWindowFlags_AlwaysAutoResize;

		ImGui::Begin(windowName, NULL, ImGuiWindowFlags_Toolbar);

		updateIconBar(state, context, pasteData);
		updateEditBar(edit);

		ImGui::PopStyleVar();

		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
	}

	void EditorToolbar::updateIconBar(ScoreEditorState& state, ScoreContext* context,
	                                  PasteData& pasteData)
	{
		auto& input = getConfig().input;

		if (iconButton(ICON_FA_FILE, "__bind_create", create, input.create))
			state.wantCreateScore = true;

		if (iconButton(ICON_FA_FOLDER_OPEN, "__bind_open", open, input.open))
			state.wantOpenScore = true;

		if (iconButton(ICON_FA_SAVE, "__bind_save", save, input.save))
			state.wantSaveScore = true;

		if (iconButton(ICON_FA_FILE_EXPORT, "__bind_export", exportScore, input.exportScore))
			state.wantExportScore = true;

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		if (iconButton(ICON_FA_CUT, "__bind_cut", cutSelection, input.cutSelection,
		               context && !context->selection.empty()))
			context->cutSelection();

		if (iconButton(ICON_FA_COPY, "__bind_copy", copySelection, input.copySelection,
		               context && !context->selection.empty()))
			context->copySelection();

		if (iconButton(ICON_FA_PASTE, "__bind_paste", paste, input.paste))
			pasteData.startPaste();

		if (iconButton(ICON_FA_CLONE, "__bind_duplicate", duplicate, input.duplicate,
		               context && !context->selection.empty()))
		{
			context->copySelection();
			pasteData.startPaste();
		}

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		if (iconButton(ICON_FA_UNDO, "__binding_undo", undo, input.undo,
		               context && context->history.hasUndo()))
			context->undo();

		if (iconButton(ICON_FA_REDO, "__binding_redo", redo, input.redo,
		               context && context->history.hasRedo()))
			context->redo();

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 4);
	}

	static int getInsertEnumRange(InsertMode mode)
	{
		switch (mode)
		{
		case InsertMode::InsertLong:
			return int(EaseType::EaseTypeCount);
		case InsertMode::InsertFlick:
			return int(FlickType::FlickTypeCount);
		case InsertMode::InsertLongMid:
			return int(EditHoldStepType::HoldStepTypeCount);
		case InsertMode::InsertGuide:
			return int(GuideColor::GuideColorCount);
		case InsertMode::InsertHiSpeed:
			return 2;
		default:
			return 0;
		}
	}

	template <typename T>
	static void insertModeComboItems(InsertMode mode, EditArgs& edit, T EditArgs::* type,
	                                 const std::string_view texts[], T max, T min = T(0))
	{
		auto& resource = getResources().timelineResources;
		const Texture* texture = resource.getToolbarTexture();
		const Sprite* sprite;
		auto& styleColors = ImGui::GetStyle().Colors;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, UI::scale({ 2, 2 }));
		ImGui::PushStyleColor(ImGuiCol_Header, styleColors[ImGuiCol_TabActive]);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, styleColors[ImGuiCol_TabSelected]);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, styleColors[ImGuiCol_ButtonHovered]);
		T currentType = edit.*type;
		for (int i = int(min); i < int(max); i++)
		{
			edit.*type = T(i);
			const bool selected = edit.*type == currentType;
			ImGui::PushID(i);
			if (ImGui::Selectable("", selected, ImGuiSelectableFlags_None, UI::toolbarBtnImgSize))
			{
				currentType = edit.*type;
				edit.insertMode = mode;
			}
			UI::tooltip(localize(texts[i]));
			if (texture && (sprite = resource.getInsertModeSprite(mode, edit)))
			{
				ImGui::SameLine(ImGui::GetStyle().WindowPadding.x, 0);
				auto&& [uv0, uv1] = texture->getCoords(*sprite);
				ImGui::Image(texture->getID(), UI::toolbarBtnImgSize, uv0, uv1);
			}
			ImGui::PopID();
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		edit.*type = currentType;
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
	}

	static constexpr std::string_view insertModeId[] = {
		"__binding_select", "__binding_tap",      "__binding_hold",     "__binding_hold_mid",
		"__binding_flick",  "__binding_critical", "__binding_friction", "__binding_guide",
		"__binding_damage", "__binding_dummy",    "__binding_bpm",      "__binding_time_sig",
		"__binding_hispeed"
	};
	static_assert(std::size(insertModeId) == size_t(InsertMode::InsertModeMax),
	              "Missing or extra bindings");

	static constexpr InsertMode specialModes[] = { InsertMode::InsertLong,
		                                           InsertMode::InsertLongMid,
		                                           InsertMode::InsertFlick,
		                                           InsertMode::InsertGuide };

	void EditorToolbar::updateEditBar(EditArgs& edit)
	{
		auto& input = getConfig().input;
		auto& resource = getResources().timelineResources;
		const Texture* texture = resource.getToolbarTexture();
		float insertXOffset[size_t(InsertMode::InsertModeMax)] = {};

		for (auto&& mode : EnumRange(InsertMode::InsertModeMax))
		{
			size_t modeIdx = static_cast<size_t>(mode);
			const Sprite* sprite = resource.getInsertModeSprite(mode, edit);
			bool special =
			    std::binary_search(std::begin(specialModes), std::end(specialModes), mode);
			insertXOffset[modeIdx] = ImGui::GetCursorScreenPos().x;
			if (imageButton(texture, sprite, insertModeTexts[modeIdx], insertModeId[modeIdx],
			                insertInputs[modeIdx], input.*insertModeBindings[modeIdx], true,
			                mode == edit.insertMode))
				edit.changeInsertMode(mode);
			if (special && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
			{
				ImGui::OpenPopup("##extended_mode_selector");
				insertModePopup = modeIdx;
			}
		}

		ImGui::PushStyleVarX(ImGuiStyleVar_WindowPadding, UI::scale(4.0f));
		ImGui::SetNextWindowPos(
		    { insertXOffset[insertModePopup] - ImGui::GetStyle().WindowPadding.x,
		      ImGui::GetCursorScreenPos().y + ImGui::GetItemRectSize().y },
		    ImGuiCond_Appearing);
		if (ImGui::BeginPopup("##extended_mode_selector",
		                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
		{
			switch ((InsertMode)insertModePopup)
			{
			case InsertMode::InsertLong:
				insertModeComboItems(InsertMode::InsertLong, edit, &EditArgs::easeType,
				                     easeTypeTexts, EaseType::EaseTypeCount);
				break;
			case InsertMode::InsertFlick:
				insertModeComboItems(InsertMode::InsertFlick, edit, &EditArgs::flickType,
				                     flickTypeTexts, FlickType::FlickTypeCount, FlickType::Default);
				break;
			case InsertMode::InsertLongMid:
				insertModeComboItems(InsertMode::InsertLongMid, edit, &EditArgs::stepType,
				                     stepTypeTexts, EditHoldStepType::HoldStepTypeCount);
				break;
			case InsertMode::InsertGuide:
				insertModeComboItems(InsertMode::InsertGuide, edit, &EditArgs::colorType,
				                     guideColorAllTexts, GuideColor::GuideColorCount);
				break;
			}

			ImGui::EndPopup();
		}
	}

	void GenericDialog::open(DialogContent content)
	{
		std::lock_guard<std::mutex> lock(contentMutex);
		pendingDialogs.push(std::move(content));
	}

	void GenericDialog::open(std::string title, std::vector<std::string> contents,
	                         std::vector<DialogContent::Action> actions)
	{
		open({ std::move(title), std::move(contents), std::move(actions) });
	}

	void GenericDialog::update()
	{
		if (pendingDialogs.size() && !ImGui::IsPopupOpen(windowName))
		{
			std::lock_guard<std::mutex> lock(contentMutex);
			ImGui::OpenPopup(windowName);
			currentName = IO::formatString("%s%s", pendingDialogs.front().title, windowName);
		}

		bool closing = false;
		ImVec2& spacing = ImGui::GetStyle().ItemSpacing;
		ImVec2& padding = ImGui::GetStyle().WindowPadding;
		ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImVec2 windowMaxSize = viewport->WorkSize / 2;
		ImVec2 titleSize = ImGui::CalcTextSize(currentName.c_str(), 0, true);
		titleSize.x += padding.x * 2;
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, { 0.5f, 0.5f });
		ImGui::SetNextWindowSizeConstraints({ titleSize.x, FLT_MIN }, windowMaxSize);
		if (ImGui::BeginPopupModal(currentName.c_str(), NULL,
		                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
		                               ImGuiWindowFlags_NoSavedSettings))
		{
			std::lock_guard<std::mutex> lock(contentMutex);
			const auto& currentDialog = pendingDialogs.front();
			ImVec2 childSize = { std::max(350.f, titleSize.x), 250 };
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::SetNextWindowSizeConstraints(childSize, { FLT_MAX, FLT_MAX });
			if (ImGui::BeginChild(ImGuiID(1), { 0, -ImGui::GetFrameHeightWithSpacing() },
			                      ImGuiChildFlags_PaddedBorder | ImGuiChildFlags_AlwaysAutoResize |
			                          ImGuiChildFlags_AutoResizeX))
			{
				for (auto&& content : currentDialog.contents)
				{
					if (content.size())
						ImGui::Text(content.c_str());
					else
						ImGui::Separator();
				}
				childSize = ImGui::GetWindowSize();
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			float availWidth = childSize.x;
			float btnWidth = (availWidth + spacing.x) / currentDialog.actions.size() - spacing.x;
			for (auto&& [name, callback] : currentDialog.actions)
			{
				if (ImGui::Button(name.c_str(), { btnWidth, 0 }))
				{
					if (callback)
						callback();
					closing = true;
				}
				ImGui::SameLine();
			}
			if (closing)
			{
				ImGui::CloseCurrentPopup();
				pendingDialogs.pop();
			}
			ImGui::EndPopup();
		}
	}

	const char* ScorePropertiesWindow::getWindowName()
	{
		auto fetchName = []() { return UI::iconTitle(ICON_FA_ALIGN_LEFT, Text::chartProperties); };
		return localizeOrInsert({ "__chart_properties_window" }, fetchName);
	}

	void ScorePropertiesWindow::update(ScoreEditorTimeline& timeline, ScoreContext& context,
	                                   Audio::AudioManager& manager, GenericDialog& dialog)
	{
		auto metadataName = []() { return UI::iconTitle(ICON_FA_ALIGN_LEFT, Text::metadata); };
		if (ImGui::CollapsingHeader(
		        localizeOrInsert({ "__chart_properties_metadata" }, metadataName),
		        ImGuiTreeNodeFlags_DefaultOpen))
		{
			UI::beginPropertyTable();
			UI::stringPropertyRow(Text::title, context.workingMetadata.title);
			if (ImGui::IsItemDeactivatedAfterEdit())
				context.pushWorkingMetadata(&ScoreMetadata::title, "Change score title");
			UI::stringPropertyRow(Text::designer, context.workingMetadata.author);
			if (ImGui::IsItemDeactivatedAfterEdit())
				context.pushWorkingMetadata(&ScoreMetadata::author, "Change score designer");
			UI::stringPropertyRow(Text::artist, context.workingMetadata.artist);
			if (ImGui::IsItemDeactivatedAfterEdit())
				context.pushWorkingMetadata(&ScoreMetadata::artist, "Change score artist");

			int result = UI::filePropertyRow(Text::jacket, context.workingMetadata.jacketFile);
			if (result > 0)
			{
				context.pushWorkingMetadata(&ScoreMetadata::jacketFile, "Change score jacket");
			}
			else if (result < 0)
			{
				IO::FileDialog fileDialog{};
				fileDialog.title = "Open Image File";
				fileDialog.filters = { IO::imageFilter, IO::allFilter };
				fileDialog.parentWindowHandle = Application::getAppWindowHandle();

				if (fileDialog.openFile() == IO::FileDialogResult::OK)
				{
					context.workingMetadata.jacketFile = fileDialog.outputFilename;
					context.pushWorkingMetadata(&ScoreMetadata::jacketFile, "Change score jacket");
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				timeline.getJacket().draw();

			bool isExtend = context.metadata.isExtendedScore;
			if (UI::checkboxPropertyRow(Text::extendedScore, isExtend))
			{
				if (!isExtend)
				{
					std::string title = UI::modalTitle(Text::extendedScore);
					std::string content = localize(Text::warnScoreExtensionDisable).string;
					DialogContent::Action onYes = { localize(Text::yes).string,
						                            [&]() { context.setScoreExtension(false); } };
					DialogContent::Action onNo = { localize(Text::no).string, nullptr };
					dialog.open(title, { content }, { onYes, onNo });
				}
				else
					context.setScoreExtension(true);
			}
			if (context.metadata.isExtendedScore)
			{
				// Not recorded by history
				UI::intPropertyRow(Text::laneExtension, context.metadata.laneExtension, "%d", 0,
				                   10000);
				UI::intPropertyRow(Text::lifePoint, context.workingMetadata.baseLifePoint, "%d", 10,
				                   1000000);
				if (ImGui::IsItemDeactivatedAfterEdit())
					context.pushWorkingMetadata(&ScoreMetadata::baseLifePoint,
					                            "Change score inital life");
			}
			UI::endPropertyTable();
		}

		auto audioVolName = []() { return UI::iconTitle(ICON_FA_VOLUME_UP, Text::audio); };
		if (ImGui::CollapsingHeader(localizeOrInsert("__chart_properties_audio", audioVolName),
		                            ImGuiTreeNodeFlags_DefaultOpen))
		{
			UI::beginPropertyTable();

			bool isMusicLoading = context.isMusicLoading->load();
			ImGui::BeginDisabled(isMusicLoading);
			int filePickResult = UI::filePropertyRow(
			    Text::musicFile, isMusicLoading ? loadingText : context.workingMetadata.musicFile);
			ImGui::EndDisabled();
			if (filePickResult > 0)
			{
				context.isPendingLoadMusic = true;
				context.pushWorkingMetadata(&ScoreMetadata::musicFile, "Change score music", true);
			}
			else if (filePickResult < 0)
			{
				IO::FileDialog fileDialog{};
				fileDialog.title = "Open Audio File";
				fileDialog.filters = { IO::audioFilter, IO::allFilter };
				fileDialog.parentWindowHandle = Application::getAppWindowHandle();

				if (fileDialog.openFile() == IO::FileDialogResult::OK)
				{
					context.workingMetadata.musicFile = fileDialog.outputFilename;
					context.isPendingLoadMusic = true;
					context.pushWorkingMetadata(&ScoreMetadata::musicFile, "Change score music",
					                            true);
				}
			}

			ImGui::BeginDisabled(isMusicLoading);
			if (UI::dragFloatPropertyRow(Text::musicOffset, context.workingMetadata.musicOffset,
			                             "%.3fms"))
				context.isPendingChangeMusicOffset = true; // live preview while dragging
			if (ImGui::IsItemDeactivatedAfterEdit())
				context.pushWorkingMetadata(&ScoreMetadata::musicOffset,
				                            "Change score music offset");
			ImGui::EndDisabled();

			// volume controls
			float master = manager.getMasterVolume();
			float bgm = manager.getMusicVolume();
			float se = manager.getSoundEffectsVolume();

			UI::percentSliderPropertyRow(Text::volumeMaster, master);
			UI::percentSliderPropertyRow(Text::volumeBgm, bgm);
			UI::percentSliderPropertyRow(Text::volumeSe, se);

			if (master != manager.getMasterVolume())
				manager.setMasterVolume(master);

			if (bgm != manager.getMusicVolume())
				manager.setMusicVolume(bgm);

			if (se != manager.getSoundEffectsVolume())
				manager.setSoundEffectsVolume(se);

			UI::labelPropertyColumn(localize(Text::playbackSpeed));

			ImGui::TableNextColumn();
			bool canDecreaseSpeed =
			    timeline.getPlaybackSpeed() > ScoreEditorTimeline::MIN_PLAYBACK_SPEED;
			bool canIncreaseSpeed =
			    timeline.getPlaybackSpeed() < ScoreEditorTimeline::MAX_PLAYBACK_SPEED;
			if (UI::transparentButton(ICON_FA_MINUS, UI::btnNormal, false, canDecreaseSpeed))
				timeline.setPlaybackSpeed(timeline.getPlaybackSpeed() - 0.25f);
			ImGui::SameLine(0, 0);
			UI::transparentButton(
			    IO::formatString("%.0f%%", timeline.getPlaybackSpeed() * 100).c_str(),
			    { -UI::btnNormal.x, UI::btnNormal.y }, false, false);
			ImGui::SameLine(0, 0);
			if (UI::transparentButton(ICON_FA_PLUS, UI::btnNormal, false, canIncreaseSpeed))
				timeline.setPlaybackSpeed(timeline.getPlaybackSpeed() + 0.25f);

			UI::endPropertyTable();
		}

		auto statisticName = []() { return UI::iconTitle(ICON_FA_CHART_BAR, Text::statistics); };
		if (ImGui::CollapsingHeader(localizeOrInsert("__chart_properties_statistic", statisticName),
		                            ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto tableFlags = (ImGuiTableFlags_PropertyTable & ~ImGuiTableFlags_Resizable) |
			                  ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg;
			UI::beginPropertyTable("stats_table", 2, tableFlags);

			ImGui::TableNextColumn();
			ImVec4 color = generateHighlightColor(ImGui::ColorConvertU32ToFloat4(speedColor));
			ImGui::TextColored(color, localize(Text::hispeeds));
			ImGui::TableNextColumn();
			if (context.score.layers.size() > 1)
			{
				ImGui::Text("%zd (%s) |\t",
				            context.score.layers[context.selectedLayer].hiSpeedChanges.size(),
				            context.score.layers[context.selectedLayer].name.c_str());
				ImGui::SameLine(0, 0);
			}
			ImGui::Text("%d", context.scoreStats.hispeeds);
			if (context.score.layers.size() > 1)
			{
				ImGui::SameLine(0, 0);
				ImGui::Text(" (%s)", (const char*)localize(Text::total));
			}

			ImVec2 btnSize = UI::scale({ 18, 18 });

			UI::statPropertyColumn(Text::taps, (int)InsertMode::InsertTap, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.taps);

			UI::statPropertyColumn(Text::flicks, (int)InsertMode::InsertFlick, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.flicks);

			UI::statPropertyColumn(Text::holds, (int)InsertMode::InsertLong, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.holds);

			UI::statPropertyColumn(Text::steps, (int)InsertMode::InsertLongMid, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.ticks);

			UI::statPropertyColumn(Text::guide, (int)InsertMode::InsertGuide, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.guides);

			UI::statPropertyColumn(Text::trace, (int)InsertMode::MakeFriction, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.traces);

			UI::statPropertyColumn(Text::damage, (int)InsertMode::InsertDamage, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.damages);

			UI::statPropertyColumn(Text::dummy, (int)InsertMode::MakeDummy, btnSize);
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.dummies);

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(localize(Text::total));
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.total);

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(localize(Text::combo));
			ImGui::TableNextColumn();
			ImGui::Text("%d", context.scoreStats.combo);
			UI::endPropertyTable();
		}
	}

	template <typename T, typename ListT, typename GetValueFn, typename PrereqFn>
	static inline bool checkMixState(T& value, const ListT& values, GetValueFn&& getValue,
	                                 PrereqFn&& isValid)
	{
		bool mixed = false;
		auto it = std::begin(values), end = std::end(values);
		for (; it != end; ++it)
		{
			if (!isValid(*it))
				continue;
			value = getValue(*it);
			break;
		}
		for (; it != end; ++it)
		{
			if (!isValid(*it) || value == getValue(*it))
				continue;
			mixed = true;
		}
		return mixed;
	}

	void ScoreNotePropertiesWindow::updateState(const ScoreContext& context)
	{
		mixedLayer = false;
		layer = context.selectedLayer;
		if (!context.selection.emptyNotes())
		{
			using value_type = NoteViewCollection::value_type;
			auto alwaysTrue = [](const value_type& v) { return true; };
			auto getTick = [](const value_type& v) { return v.second->tick; };
			auto getLane = [](const value_type& v) { return v.second->lane; };
			auto getWidth = [](const value_type& v) { return v.second->width; };
			auto getCrit = [](const value_type& v) { return v.second->isCrit(); };
			auto canCrit = [](const value_type& v) { return v.second->canCrit(); };
			auto getTrace = [](const value_type& v) { return v.second->isTrace(); };
			auto canTrace = [](const value_type& v) { return v.second->canTrace(); };
			auto getFlick = [](const value_type& v) { return v.second->flick; };
			auto canFlick = [](const value_type& v) { return v.second->canFlick(); };
			auto getDummy = [](const value_type& v) { return v.second->isDummy(); };
			auto canDummy = [](const value_type& v) { return v.second->canDummy(); };
			auto getEase = [](const value_type& v) { return v.second->ease; };
			auto canEase = [](const value_type& v) { return v.second->hasEase(); };
			auto getType = [](const value_type& v)
			{
				if (v.second->isHidden())
					return EditHoldStepType::Hidden;
				return v.second->isAttached() ? EditHoldStepType::Skip : EditHoldStepType::Normal;
			};
			auto canSetSfx = [](const value_type& v) { return v.second->canSoundEffect(); };
			auto getSfx = [](const value_type& v) { return v.second->soundEffect; };
			auto isHold = [](const value_type& v) { return v.second->isHold(); };
			auto isNormalHold = [&](const value_type& v)
			{
				return v.second->isHold() && !context.score.holdNotes.at(v.second->holdID)
				                                  .holdStepAt(*v.second, context.score.notes)
				                                  .isGuide();
			};
			auto getCritHold = [&](const value_type& v)
			{
				return v.second->isHold() && context.score.holdNotes.at(v.second->holdID)
				                                 .holdStepAt(*v.second, context.score.notes)
				                                 .isCrit();
			};
			auto getDummyHold = [&](const value_type& v)
			{
				return v.second->isHold() && context.score.holdNotes.at(v.second->holdID)
				                                 .holdStepAt(*v.second, context.score.notes)
				                                 .isDummy();
			};
			auto getSeparatorHold = [&](const value_type& v)
			{
				return v.second->isHold() && context.score.holdNotes.at(v.second->holdID)
				                                     .holdStepAt(*v.second, context.score.notes)
				                                     .ID == v.first;
			};
			auto isHoldMid = [&](const value_type& v)
			{
				if (!v.second->isHold())
					return false;
				const HoldNote& hold = context.score.holdNotes.at(v.second->holdID);
				return v.first != hold.steps.front() && v.first != hold.steps.back();
			};
			auto isGuideHold = [&](const value_type& v)
			{
				return v.second->isHold() && context.score.holdNotes.at(v.second->holdID)
				                                 .holdStepAt(*v.second, context.score.notes)
				                                 .isGuide();
			};
			auto hasAnyGuideHold = [&](const value_type& v)
			{
				if (!v.second->isHold())
					return false;
				const HoldNote& hold = context.score.holdNotes.at(v.second->holdID);
				return std::any_of(hold.separators.begin(), hold.separators.end(),
				                   [&](auto&& s) { return s.isGuide(); });
			};
			auto canHaveAlpha = [&](const value_type& v)
			{
				return v.second->isHold() && context.score.holdNotes.at(v.second->holdID)
				                                 .canGuideAlpha(*v.second, context.score.notes);
			};
			auto getGuideCol = [&](const value_type& v)
			{
				return context.score.holdNotes.at(v.second->holdID)
				    .holdStepAt(*v.second, context.score.notes)
				    .guideColor;
			};
			auto getFadeType = [&](const value_type& v)
			{ return context.score.holdNotes.at(v.second->holdID).fadeType; };
			auto getHoldLayer = [&](const value_type& v)
			{
				return context.score.holdNotes.at(v.second->holdID)
				    .holdStepAt(*v.second, context.score.notes)
				    .layer;
			};
			auto getAlpha = [](const value_type& v) { return v.second->guideAlpha; };
			auto getLayer = [](const value_type& v) { return v.second->layer; };
			bool isCrit = false, isCritHold = false, isTrace = false;
			bool isDummy = false, isDummyHold = false;
			mixedTick = checkMixState(tick, context.selectedNotes, getTick, alwaysTrue);
			quarter = static_cast<qnote_t>(tick) / TICKS_PER_QUARTER;
			mixedLane = checkMixState(lane, context.selectedNotes, getLane, alwaysTrue);
			mixedWidth = checkMixState(width, context.selectedNotes, getWidth, alwaysTrue);
			mixedCritical = checkMixState(isCrit, context.selectedNotes, getCrit, canCrit);
			mixedTrace = hasFlag(context.selectedFlag, SelectionFlag::CanTrace) &&
			             checkMixState(isTrace, context.selectedNotes, getTrace, canTrace);
			mixedFlick = hasFlag(context.selectedFlag, SelectionFlag::CanFlick) &&
			             checkMixState(flick, context.selectedNotes, getFlick, canFlick);
			mixedDummy = hasFlag(context.selectedFlag, SelectionFlag::CanDummy) &&
			             checkMixState(isDummy, context.selectedNotes, getDummy, canDummy);
			mixedEase = hasFlag(context.selectedFlag, SelectionFlag::CanEase) &&
			            checkMixState(easeType, context.selectedNotes, getEase, canEase);
			mixedSoundEffect = hasFlag(context.selectedFlag, SelectionFlag::CanSoundEffect) &&
			                   checkMixState(soundEffect, context.selectedNotes, getSfx, canSetSfx);
			mixedStep = checkMixState(stepType, context.selectedNotes, getType, isHold);
			mixedHoldCrit =
			    hasFlag(context.selectedFlag, SelectionFlag::HasHoldNote) &&
			    checkMixState(isCritHold, context.selectedNotes, getCritHold, isNormalHold);
			mixedHoldDummy =
			    hasFlag(context.selectedFlag, SelectionFlag::HasHoldNote) &&
			    checkMixState(isDummyHold, context.selectedNotes, getDummyHold, isNormalHold);
			mixedHoldSeparator =
			    hasFlag(context.selectedFlag, SelectionFlag::HasAnyHoldMid) &&
			    checkMixState(holdSeparator, context.selectedNotes, getSeparatorHold, isHoldMid);
			layer = -1;
			mixedLayer = context.metadata.isExtendedScore &&
			             checkMixState(layer, context.selectedNotes, getLayer, alwaysTrue);
			mixedGuideCol =
			    hasFlag(context.selectedFlag, SelectionFlag::HasGuideNote) &&
			    checkMixState(guideCol, context.selectedNotes, getGuideCol, isGuideHold);
			mixedFade =
			    checkMixState(fadeType, context.selectedNotes, getFadeType, hasAnyGuideHold);
			mixedHoldLayer = checkMixState(holdLayer, context.selectedNotes, getHoldLayer, isHold);
			mixedAlpha = checkMixState(alpha, context.selectedNotes, getAlpha, canHaveAlpha);
			noteFlag = setFlag(noteFlag, NoteFlag::Critical, isCrit);
			noteFlag = setFlag(noteFlag, NoteFlag::Trace, isTrace);
			noteFlag = setFlag(noteFlag, NoteFlag::Dummy, isDummy);
			holdFlag = setFlag(holdFlag, HoldNoteFlag::Critical, isCritHold);
			holdFlag = setFlag(holdFlag, HoldNoteFlag::Dummy, isDummyHold);
		}
		if (!context.selection.emptyHispeeds())
		{
			using value_type = HiSpeedRefCollection::value_type;
			auto alwaysTrue = [](const value_type& v) { return true; };
			auto getLayer = [&](const value_type& v) { return v.first; };
			auto getSpeed = [&](const value_type& v)
			{ return context.score.layers[v.first].hiSpeedChanges.at(v.second).speed; };
			auto getSkips = [&](const value_type& v)
			{ return context.score.layers[v.first].hiSpeedChanges.at(v.second).skips; };
			auto getEase = [&](const value_type& v)
			{ return context.score.layers[v.first].hiSpeedChanges.at(v.second).ease; };
			auto getHideNotes = [&](const value_type& v)
			{ return context.score.layers[v.first].hiSpeedChanges.at(v.second).hideNotes; };
			id_t prevLayer = layer;
			mixedLayer = !mixedLayer &&
			             checkMixState(layer, context.selection.hispeeds, getLayer, alwaysTrue);
			if (prevLayer != layer && !context.selection.emptyNotes())
				mixedLayer = true;
			mixedSpeed = checkMixState(speed, context.selection.hispeeds, getSpeed, alwaysTrue);
			mixedSkips = context.metadata.isExtendedScore &&
			             checkMixState(skips, context.selection.hispeeds, getSkips, alwaysTrue);
			mixedhspdEase =
			    context.metadata.isExtendedScore &&
			    checkMixState(hspdEase, context.selection.hispeeds, getEase, alwaysTrue);
			mixedHideNotes =
			    context.metadata.isExtendedScore &&
			    checkMixState(hideNotes, context.selection.hispeeds, getHideNotes, alwaysTrue);
		}
	}

	const char* ScoreNotePropertiesWindow::getWindowName()
	{
		auto fetchName = []() { return UI::iconTitle(ICON_FA_WRENCH, Text::noteProperties); };
		return localizeOrInsert({ "__note_properties_window" }, fetchName);
	}

	void ScoreNotePropertiesWindow::update(ScoreContext& context)
	{
		if (context.selection.empty())
		{
			ImGui::TextUnformatted(localize(Text::notePropertiesNotSelected));
			return;
		}
		if (hasFlag(context.selectedFlag, SelectionFlag::DirtyProperty))
		{
			updateState(context);
			context.selectedFlag =
			    setFlag(context.selectedFlag, SelectionFlag::DirtyProperty, false);
		}
		if (!context.selection.emptyNotes())
		{
			const char* notePropsTitle = localizeOrInsert("__option_note_props", UI::iconTitle,
			                                              ICON_FA_COG, Text::notePropertiesNote);
			if (ImGui::CollapsingHeader(notePropsTitle, ImGuiTreeNodeFlags_DefaultOpen))
			{
				UI::beginPropertyTable();
				int state;
				state = UI::mixedFloatPropertyRow(Text::beat, mixedTick, quarter, "%.3f", 0,
				                                  MAX_QUARTERS);
				if (state < 0)
				{
					tick_t offset = std::round(quarter * TICKS_PER_QUARTER) - tick;
					float l = 0;
					if (context.canMoveNoteSelection(offset, 1, l, 12, SnapMode::Relative))
					{
						context.moveNoteSelection(offset, 1, l, 12, SnapMode::Relative,
						                          ImGui::IsItemDeactivated());
						tick += offset;
					}
					else
					{
						quarter = ticksToQuarters(tick);
						if (ImGui::IsItemDeactivated())
							context.pushSelectionChanged("Move note");
					}
				}
				else if (state > 0)
				{
					tick = std::round(quarter * TICKS_PER_QUARTER);
					context.setPosNoteSelection(tick, NAN);
					context.updateSelectionFlag();
				}

				if (getConfig().showTickInProperties)
				{
					tick_t oldTick = tick;
					state = UI::mixedIntPropertyRow(Text::tick, mixedTick, tick, "%d", 0, MAX_TICK);
					if (state < 0)
					{
						tick_t offset = tick - oldTick;
						float l = 0;
						if (context.canMoveNoteSelection(offset, 1, l, 12, SnapMode::Relative))
						{
							context.moveNoteSelection(offset, 1, l, 12, SnapMode::Relative,
							                          ImGui::IsItemDeactivated());
							quarter = static_cast<qnote_t>(tick) / TICKS_PER_QUARTER;
						}
						else
						{
							tick = oldTick;
							if (ImGui::IsItemDeactivated())
								context.pushSelectionChanged("Move note");
						}
					}
					else if (state > 0)
					{
						context.setPosNoteSelection(tick, NAN);
						quarter = static_cast<qnote_t>(tick) / TICKS_PER_QUARTER;
						context.updateSelectionFlag();
					}
				}
				if (context.metadata.isExtendedScore)
				{
					UI::labelPropertyColumn(localize(Text::layer));
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-1);
					if (ImGui::BeginCombo("##layer_combo",
					                      mixedLayer ? localize(Text::notePropertiesMixedValue)
					                                 : context.score.layers[layer].name.c_str()))
					{
						for (id_t i = 0; i < context.score.layers.size(); i++)
							if (ImGui::Selectable(context.score.layers[i].name.c_str(), layer == i))
								context.setLayer(i);
						ImGui::EndCombo();
					}
				}

				float oldLane = lane;
				state = UI::mixedFloatPropertyRow(Text::lane, mixedLane, lane, "%.2f",
				                                  context.minLane(), context.maxLane());
				if (state && !context.metadata.isExtendedScore)
					lane = std::floor(lane);
				if (state < 0)
				{
					tick_t t = 0;
					float offset = lane - oldLane;
					if (context.canMoveNoteSelection(t, 1, offset, 12, SnapMode::Relative))
					{
						context.moveNoteSelection(t, 1, offset, 12, SnapMode::Relative,
						                          ImGui::IsItemDeactivated());
					}
					else
					{
						lane = oldLane;
						if (ImGui::IsItemDeactivated())
							context.pushSelectionChanged("Move note");
					}
				}
				else if (state > 0)
				{
					context.setPosNoteSelection(MAX_TICK, lane);
					context.updateSelectionFlag();
				}

				float oldWidth = width;
				state = UI::mixedFloatPropertyRow(Text::width, mixedWidth, width, "%.2f",
				                                  context.minNoteWidth(), context.maxNoteWidth());
				if (state && !context.metadata.isExtendedScore)
					width = std::floor(width);
				if (state < 0)
				{
					float offset = width - oldWidth;
					for (auto& [id, pnote] : context.selectedNotes)
						pnote->width = std::clamp(pnote->width + offset, context.minNoteWidth(),
						                          context.maxNoteWidth(pnote->lane));
					if (ImGui::IsItemDeactivated())
					{
						context.pushSelectionChanged("Edit note width");
						context.updateSelectionFlag();
					}
				}
				else if (state > 0)
				{
					for (auto& [id, pnote] : context.selectedNotes)
						pnote->width = std::clamp(width, context.minNoteWidth(),
						                          context.maxNoteWidth(pnote->lane));
					context.pushSelectionChanged("Edit note width");
					context.updateSelectionFlag();
				}

				if (hasFlag(context.selectedFlag, SelectionFlag::CanTrace) &&
				    UI::checkboxFlagPropertyRow(Text::trace, noteFlag, NoteFlag::Trace, mixedTrace))
					context.setFriction(hasFlag(noteFlag, NoteFlag::Trace));

				if (hasFlag(context.selectedFlag, SelectionFlag::CanCritical) &&
				    UI::checkboxFlagPropertyRow(Text::critical, noteFlag, NoteFlag::Critical,
				                                mixedCritical))
					context.setCriticals(hasFlag(noteFlag, NoteFlag::Critical));

				if (hasFlag(context.selectedFlag, SelectionFlag::CanFlick) &&
				    UI::selectMixedPropertyRow(Text::flick, mixedFlick, flick,
				                               Text::notePropertiesMixedValue, flickTypeTexts,
				                               context.maxFlick()))
					context.setFlick(flick);

				if (hasFlag(context.selectedFlag, SelectionFlag::CanDummy) &&
				    UI::checkboxFlagPropertyRow(Text::dummy, noteFlag, NoteFlag::Dummy, mixedDummy))
					context.setDummy(hasFlag(noteFlag, NoteFlag::Dummy));

				if (hasFlag(context.selectedFlag, SelectionFlag::CanSoundEffect) &&
				    UI::selectMixedPropertyRow(Text::soundEffect, mixedSoundEffect, soundEffect,
				                               Text::notePropertiesMixedValue, soundEffectTexts))
					context.setSoundEffect(soundEffect);

				UI::endPropertyTable();
			}

			const char* holdPropsTitle = localizeOrInsert(
			    "__option_hold_props", UI::iconTitle, ICON_FA_COG, Text::notePropertiesHoldNote);
			if ((hasFlag(context.selectedFlag, SelectionFlag::HasHoldNote) ||
			     hasFlag(context.selectedFlag, SelectionFlag::HasGuideNote)) &&
			    ImGui::CollapsingHeader(holdPropsTitle, ImGuiTreeNodeFlags_DefaultOpen))
			{
				UI::beginPropertyTable();
				if (UI::selectMixedPropertyRow(Text::holdStep, mixedStep, stepType,
				                               Text::notePropertiesMixedValue, stepTypeTexts))
					context.setStep(stepType);

				if (hasFlag(context.selectedFlag, SelectionFlag::CanEase) &&
				    UI::selectMixedPropertyRow(Text::easeType, mixedEase, easeType,
				                               Text::notePropertiesMixedValue, easeTypeTexts,
				                               context.maxEase()))
					context.setEase(easeType);

				if (context.metadata.isExtendedScore &&
				    hasFlag(context.selectedFlag, SelectionFlag::HasHoldNote) &&
				    UI::checkboxFlagPropertyRow(Text::holdDummy, holdFlag, HoldNoteFlag::Dummy,
				                                mixedHoldDummy))
					context.setDummyHold(hasFlag(holdFlag, HoldNoteFlag::Dummy));

				if (hasFlag(context.selectedFlag, SelectionFlag::HasHoldNote) &&
				    UI::checkboxFlagPropertyRow(Text::holdCritical, holdFlag,
				                                HoldNoteFlag::Critical, mixedHoldCrit))
					context.setCriticalHold(hasFlag(holdFlag, HoldNoteFlag::Critical));

				enum class SeperatorFlag : uint8_t
				{
					False,
					True
				} separatorState = holdSeparator ? SeperatorFlag::True : SeperatorFlag::False;
				if (context.metadata.isExtendedScore &&
				    hasFlag(context.selectedFlag, SelectionFlag::HasAnyHoldMid) &&
				    UI::checkboxFlagPropertyRow(Text::holdSeparator, separatorState,
				                                SeperatorFlag::True, mixedHoldSeparator))
					context.setHoldSeparator(holdSeparator = separatorState == SeperatorFlag::True);

				ImGui::BeginDisabled(!context.metadata.isExtendedScore);
				if (UI::selectMixedPropertyRow(Text::layer, mixedHoldLayer, holdLayer,
				                               Text::notePropertiesMixedValue, holdLayerTexts))
					context.setHoldLayer(holdLayer);
				ImGui::SetItemTooltip((const char*)localize(Text::holdLayerTooltip));
				ImGui::EndDisabled();

				UI::endPropertyTable();
			}

			const char* guidePropsTitle = localizeOrInsert("__option_guide_props", UI::iconTitle,
			                                               ICON_FA_COG, Text::notePropertiesGuide);
			if (!hasFlag(context.selectedFlag, SelectionFlag::HasGuideNote))
			{
				if (hasFlag(context.selectedFlag, SelectionFlag::HasGuideAlphaNote) &&
				    ImGui::CollapsingHeader(guidePropsTitle, ImGuiTreeNodeFlags_DefaultOpen))
				{
					UI::beginPropertyTable();
					ImGui::BeginDisabled(!context.metadata.isExtendedScore);
					if (UI::selectMixedPropertyRow(Text::fadeType, mixedFade, fadeType,
					                               Text::notePropertiesMixedValue, fadeTypeTexts))
						context.setFadeType(fadeType);
					ImGui::EndDisabled();
					ImGui::BeginDisabled(
					    mixedFade ||
					    !hasFlag(context.selectedFlag, SelectionFlag::CanSetGuideAlphaNote));
					alpha = std::abs(alpha);
					float inpAlpha = alpha * 100;
					if (UI::mixedFloatPropertyRow(Text::stepAlpha, mixedAlpha, inpAlpha, "%g %%", 0,
					                              100, 10))
						context.setGuideAlpha(alpha = inpAlpha / 100);
					ImGui::EndDisabled();
					UI::endPropertyTable();
				}
			}
			else if (ImGui::CollapsingHeader(guidePropsTitle, ImGuiTreeNodeFlags_DefaultOpen))
			{
				UI::beginPropertyTable();
				bool editCol = false;
				if (!context.metadata.isExtendedScore)
				{
					ClassicGuideColor classicCol = guideCol != GuideColor::Yellow
					                                   ? ClassicGuideColor::Green
					                                   : ClassicGuideColor::Yellow;
					editCol =
					    UI::selectMixedPropertyRow(Text::guideColor, mixedGuideCol, classicCol,
					                               Text::notePropertiesMixedValue, guideColorTexts);
					guideCol = classicCol != ClassicGuideColor::Yellow ? GuideColor::Green
					                                                   : GuideColor::Yellow;
				}
				else
					editCol = UI::selectMixedPropertyRow(Text::guideColor, mixedGuideCol, guideCol,
					                                     Text::notePropertiesMixedValue,
					                                     guideColorAllTexts);
				if (editCol)
					context.setGuideColor(guideCol);
				ImGui::BeginDisabled(!context.metadata.isExtendedScore);
				if (UI::selectMixedPropertyRow(Text::fadeType, mixedFade, fadeType,
				                               Text::notePropertiesMixedValue, fadeTypeTexts))
					context.setFadeType(fadeType);
				ImGui::EndDisabled();
				ImGui::BeginDisabled(mixedFade || !hasFlag(context.selectedFlag,
				                                           SelectionFlag::CanSetGuideAlphaNote));
				alpha = std::abs(alpha);
				float inpAlpha = alpha * 100;
				if (UI::mixedFloatPropertyRow(Text::stepAlpha, mixedAlpha, inpAlpha, "%g %%", 0,
				                              100, 10))
					context.setGuideAlpha(alpha = inpAlpha / 100);
				ImGui::EndDisabled();
				UI::endPropertyTable();
			}
		}
		if (!context.selection.emptyHispeeds() &&
		    ImGui::CollapsingHeader(localizeOrInsert("__option_hispeed_props", UI::iconTitle,
		                                             ICON_FA_FAST_FORWARD,
		                                             Text::notePropertiesHiSpeed),
		                            ImGuiTreeNodeFlags_DefaultOpen))
		{
			UI::beginPropertyTable();

			if (context.metadata.isExtendedScore)
			{
				UI::labelPropertyColumn(localize(Text::layer));
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::BeginCombo("##hispd_layer_combo",
				                      mixedLayer ? localize(Text::notePropertiesMixedValue)
				                                 : context.score.layers[layer].name.c_str()))
				{
					for (id_t i = 0; i < context.score.layers.size(); i++)
						if (ImGui::Selectable(context.score.layers[i].name.c_str(), layer == i))
							context.setLayer(i);
					ImGui::EndCombo();
				}
			}
			float oldSpeed = speed;
			int state = UI::mixedFloatPropertyRow(Text::hiSpeedSpeed, mixedSpeed, speed, "%.3f",
			                                      -MAX_HISPEED, MAX_HISPEED, 0.1);
			if (state > 0)
			{
				for (auto& [l, t] : context.selection.hispeeds)
				{
					HiSpeed& h = context.score.layers[l].hiSpeedChanges.at(t);
					h.speed = speed;
				}
				if (ImGui::IsItemDeactivated() &&
				    context.pushSelectionChanged("Change Hispeeds speed"))
					updateState(context);
			}
			else if (state < 0)
			{
				float offset = speed - oldSpeed;
				for (auto& [l, t] : context.selection.hispeeds)
				{
					HiSpeed& h = context.score.layers[l].hiSpeedChanges.at(t);
					h.speed = std::clamp(h.speed + offset, -MAX_HISPEED, MAX_HISPEED);
				}
				if (ImGui::IsItemDeactivated() &&
				    context.pushSelectionChanged("Change Hispeeds speed"))
					updateState(context);
			}
			if (context.metadata.isExtendedScore)
			{
				if (UI::selectMixedPropertyRow(Text::hiSpeedEase, mixedhspdEase, hspdEase,
				                               Text::notePropertiesMixedValue,
				                               hispeedEaseTypeTexts))
				{
					for (auto& [l, t] : context.selection.hispeeds)
					{
						HiSpeed& h = context.score.layers[l].hiSpeedChanges.at(t);
						h.ease = hspdEase;
					}
					if (context.pushSelectionChanged("Change Hispeeds ease"))
						updateState(context);
				}
				const char* beatFmtStr = localizeOrInsert(
				    "__fmt_beat_float",
				    []() { return IO::formatString("%%.3f %s", localize(Text::beat)); });
				float oldSkips = skips;
				state = UI::mixedFloatPropertyRow(Text::hiSpeedSkipBeat, mixedSkips, skips,
				                                  beatFmtStr, -MAX_HISPEED, MAX_HISPEED);
				if (state > 0)
				{
					for (auto& [l, t] : context.selection.hispeeds)
					{
						HiSpeed& h = context.score.layers[l].hiSpeedChanges.at(t);
						h.skips = skips;
					}
					if (ImGui::IsItemDeactivated() &&
					    context.pushSelectionChanged("Change Hispeeds skip beat"))
						updateState(context);
				}
				else if (state < 0)
				{
					float offset = skips - oldSkips;
					for (auto& [l, t] : context.selection.hispeeds)
					{
						HiSpeed& h = context.score.layers[l].hiSpeedChanges.at(t);
						h.skips = std::clamp(h.skips + offset, -MAX_HISPEED, MAX_HISPEED);
					}
					if (ImGui::IsItemDeactivated() &&
					    context.pushSelectionChanged("Change Hispeeds skip beat"))
						updateState(context);
				}
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedHideNotes);
				if (UI::checkboxPropertyRow(Text::hiSpeedHideNotes, hideNotes))
				{
					for (auto& [l, t] : context.selection.hispeeds)
					{
						HiSpeed& h = context.score.layers[l].hiSpeedChanges.at(t);
						h.hideNotes = hideNotes;
					}
					if (context.pushSelectionChanged("Change Hispeeds hide notes"))
						updateState(context);
				}
				ImGui::PopItemFlag();
			}
			UI::endPropertyTable();
		}
	}

	const char* ScoreOptionsWindow::getWindowName()
	{
		auto fetchName = []() { return UI::iconTitle(ICON_FA_WRENCH, Text::options); };
		return localizeOrInsert("__score_options_window", fetchName);
	}

	void ScoreOptionsWindow::update(ScoreContext& context, EditArgs& edit)
	{
		constexpr auto laneInputFmt = []()
		{ return IO::formatString("%%g %s", localize(Text::lane)); };
		constexpr auto beatInputFmt = []()
		{ return IO::formatString("%%.3f %s", localize(Text::beat)); };
		auto& input = getConfig().input;
		bool isExtended = context.metadata.isExtendedScore;
		UI::beginPropertyTable();
		switch (edit.insertMode)
		{
		case InsertMode::InsertBPM:
			UI::floatPropertyRow(Text::bpm, edit.bpm, "%.8g BPM", MIN_BPM, MAX_BPM);
			break;

		case InsertMode::InsertTimeSign:
			UI::timeSignatureSelect(edit.timeSignatureNumerator, edit.timeSignatureDenominator);
			break;

		case InsertMode::InsertHiSpeed:
			UI::floatPropertyRow(Text::hiSpeedSpeed, edit.hiSpeed, "%.8gx", -MAX_HISPEED,
			                     MAX_HISPEED);
			if (isExtended)
			{
				UI::selectPropertyRow(Text::hiSpeedEase, edit.hiSpeedEase, hispeedEaseTypeTexts);
				UI::floatPropertyRow(Text::hiSpeedSkipBeat, edit.hiSpeedSkip,
				                     localizeOrInsert("__options_beat_input", beatInputFmt),
				                     -MAX_HISPEED, MAX_HISPEED);
				UI::checkboxPropertyRow(Text::hiSpeedHideNotes, edit.hiSpeedHideNotes);
			}
			break;
		default: // NoteTypes
			UI::floatPropertyRow(Text::noteWidth, edit.noteWidth,
			                     localizeOrInsert("__options_lane_input", laneInputFmt),
			                     context.minNoteWidth(), context.maxNoteWidth());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
			    (input.increaseNoteSize.count || input.decreaseNoteSize.count) &&
			    ImGui::BeginTooltip())
			{
				if (input.decreaseNoteSize.count)
					ImGui::TextUnformatted(bindingLabel(Text::decreaseNoteSize,
					                                    "__bind_decrease_note_size",
					                                    decreaseNoteSize, input.decreaseNoteSize));
				if (input.increaseNoteSize.count)
					ImGui::TextUnformatted(bindingLabel(Text::increaseNoteSize,
					                                    "__bind_increase_note_size",
					                                    increaseNoteSize, input.increaseNoteSize));
				ImGui::EndTooltip();
			}
			UI::floatSliderPropertyRow(Text::noteAlign, edit.noteAlign, 0, 1, "%.2f");
			if (isExtended)
			{
				UI::selectPropertyRow(Text::soundEffect, edit.soundEffect, soundEffectTexts);
			}
			break;
		}
		switch (edit.insertMode)
		{
		case InsertMode::InsertFlick:
			UI::selectPropertyRow(Text::flick, edit.flickType, flickTypeTexts, context.maxFlick(),
			                      FlickType::Default);
			break;
		case InsertMode::InsertLong:
			UI::selectPropertyRow(Text::holdStartType, edit.startType, holdJointTypeTexts);
			UI::selectPropertyRow(Text::holdEndType, edit.endType, holdJointTypeTexts);
			UI::selectPropertyRow(Text::easeType, edit.easeType, easeTypeTexts, context.maxEase());
			if (isExtended)
			{
				UI::selectPropertyRow(Text::layer, edit.holdLayer, holdLayerTexts);
			}
			break;
		case InsertMode::InsertLongMid:
			UI::selectPropertyRow(Text::stepType, edit.stepType, stepTypeTexts);
			break;
		case InsertMode::InsertGuide:
			UI::selectPropertyRow(Text::easeType, edit.easeType, easeTypeTexts, context.maxEase());
			if (isExtended)
			{
				UI::selectPropertyRow(Text::guideColor, edit.colorType, guideColorAllTexts);
				UI::selectPropertyRow(Text::fadeType, edit.fadeType, fadeTypeTexts);
				UI::selectPropertyRow(Text::layer, edit.holdLayer, holdLayerTexts);
			}
			else
			{
				ClassicGuideColor color = edit.colorType != GuideColor::Yellow
				                              ? ClassicGuideColor::Green
				                              : ClassicGuideColor::Yellow;
				if (UI::selectPropertyRow(Text::guideColor, color, guideColorTexts))
					edit.colorType =
					    color != ClassicGuideColor::Yellow ? GuideColor::Green : GuideColor::Yellow;
			}
			break;
		}
		UI::endPropertyTable();
	}

	const char* PresetsWindow::getWindowName()
	{
		auto fetchName = []() { return UI::iconTitle(ICON_FA_DRAFTING_COMPASS, Text::presets); };
		return localizeOrInsert({ "__presets_window" }, fetchName);
	}

	void PresetsWindow::update(PresetManager& presetManager, ScoreContext& context,
	                           PasteData& pasteData)
	{
		int removePattern = -1;

		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		float filterWidth = ImGui::GetContentRegionAvail().x - UI::btnSmall.x;
		if (filterWidth != 0.0f)
			ImGui::SetNextItemWidth(filterWidth);
		auto getHintText = []()
		{ return IO::formatString("%s %s", ICON_FA_SEARCH, localize(Text::search)); };
		const char* hintText = localizeOrInsert("__hint_search", getHintText);
		if (ImGui::InputTextWithHint("##preset_filter", hintText, presetFilter.InputBuf,
		                             IM_ARRAYSIZE(presetFilter.InputBuf)))
			presetFilter.Build();
		ImGui::SameLine(0, 0);
		if (ImGui::Button(ICON_FA_TIMES, UI::btnSmall))
			presetFilter.Clear();
		ImGui::PopStyleColor();

		float presetButtonHeight = ImGui::GetFrameHeight();
		if (ImGui::BeginChild(
		        "presets_child_window",
		        { -1, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y * 3 },
		        ImGuiChildFlags_PaddedBorder))
		{
			if (!presetManager.presets.size())
			{
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().DisabledAlpha);
				UI::centeredText(localize(Text::noPresets));
				ImGui::PopStyleVar();
			}
			else
			{
				for (const auto& [id, preset] : presetManager.presets)
				{
					if (!presetFilter.PassFilter(preset.getName().c_str()))
						continue;

					ImGui::PushID(id);
					ImVec2 buttonSize = { -(UI::btnSmall.x + ImGui::GetStyle().ItemSpacing.x) * 2,
						                  presetButtonHeight };
					if (ImGui::Button(preset.getName().c_str(), buttonSize))
						presetManager.applyPreset(id, pasteData);

					if (preset.description.size())
						UI::tooltip(preset.description.c_str());

					ImGui::SameLine();
					if (UI::transparentButton(ICON_FA_EXCHANGE_ALT,
					                          { UI::btnSmall.x, presetButtonHeight }))
					{
						presetManager.applyPreset(id, pasteData);
						pasteData.flip();
					}

					ImGui::SameLine();
					if (UI::transparentButton(ICON_FA_TRASH,
					                          { UI::btnSmall.x, presetButtonHeight }))
						removePattern = id;

					ImGui::PopID();
				}
			}
		}
		ImGui::EndChild();
		ImGui::Spacing();

		ImGui::BeginDisabled(context.selection.empty());
		if (ImGui::Button(localize(Text::createPreset),
		                  ImVec2(-1, ImGui::GetFrameHeightWithSpacing())))
			dialogOpen = true;
		if (context.selection.empty() && ImGui::GetWindowDrawList())
		{
			ImGuiStyle& style = ImGui::GetStyle();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			                  ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0,
			                  style.FrameBorderSize);
		}
		ImGui::EndDisabled();

		if (removePattern != -1)
			presetManager.removePreset(removePattern);

		if (updateCreationDialog() == DialogResult::Ok)
		{
			presetManager.createPreset(context, presetName, presetDesc);
			presetName.clear();
			presetDesc.clear();
		}
	}

	DialogResult PresetsWindow::updateCreationDialog()
	{
		const char* dialogTitle =
		    localizeOrInsert("__presets_window_create_dialog", UI::modalTitle, Text::createPreset);
		if (dialogOpen)
		{
			ImGui::OpenPopup(dialogTitle);
			dialogOpen = false;
		}

		DialogResult result = DialogResult::None;

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always,
		                        { 0.5f, 0.5f });
		ImGui::SetNextWindowSize({ 500, 300 }, ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		if (ImGui::BeginPopupModal(dialogTitle, NULL, ImGuiWindowFlags_NoResize))
		{
			ImVec2 padding = ImGui::GetStyle().WindowPadding;
			ImVec2 spacing = ImGui::GetStyle().ItemSpacing;

			ImGui::TextUnformatted(localize(Text::presetName));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			bool editDone = ImGui::InputText("##preset_name", &presetName,
			                                 ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::TextUnformatted(localize(Text::description));
			ImGui::InputTextMultiline(
			    "##preset_desc", &presetDesc,
			    { -1, ImGui::GetContentRegionAvail().y - UI::btnSmall.y - 10.0f - padding.y });

			ImVec2 btnSz{ (ImGui::GetContentRegionAvail().x - spacing.x - (padding.x / 2)) / 2.0f,
				          ImGui::GetFrameHeight() };
			if (ImGui::Button(localize(Text::cancel), btnSz))
			{
				result = DialogResult::Cancel;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(presetName.empty());
			if (ImGui::Button(localize(Text::confirm), btnSz) || (editDone && presetName.size()))
			{
				result = DialogResult::Ok;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		return result;
	}

	const char* DebugWindow::getWindowName()
	{
		auto fetchName = []() { return UI::iconTitle(ICON_FA_BUG, Text::debug); };
		return localizeOrInsert({ "__debug_window" }, fetchName);
	}

	void DebugWindow::update(Audio::AudioManager& audio)
	{
		if (!ImGui::Begin(getWindowName()))
		{
			ImGui::End();
			return;
		}
		constexpr ImGuiTreeNodeFlags headerFlags = ImGuiTreeNodeFlags_DefaultOpen;
		constexpr ImGuiTreeNodeFlags treeNodeFlags = headerFlags | ImGuiTreeNodeFlags_Framed;
		if (ImGui::TreeNodeEx("Audio", treeNodeFlags))
		{
			if (ImGui::CollapsingHeader("Engine", headerFlags))
			{
				UI::beginPropertyTable();
				UI::labelPropertyRow("Sample Rate", std::to_string(audio.getDeviceSampleRate()));
				UI::labelPropertyRow("Channel Count",
				                     std::to_string(audio.getDeviceChannelCount()));
				UI::labelPropertyRow("Latency",
				                     IO::formatString("%.2fms", audio.getDeviceLatency() * 1000));
				UI::endPropertyTable();
				ImGui::Separator();
				if (ImGui::Button("Restart engine", { -1, ImGui::GetFrameHeight() }))
				{
					audio.stopEngine();
					audio.startEngine();
					audio.setAudioEngineAbsoluteTime(0);
				}
			}

			if (ImGui::CollapsingHeader("Sound Test", headerFlags))
			{
				constexpr ImGuiTableFlags tableFlags =
				    ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
				    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg;

				const int rowHeight = ImGui::GetFrameHeight() + 5;

				auto soundProfile = audio.getCurrentSoundEffectsProfile();
				if (soundProfile)
				{
					ImGui::Text("Current profile '%s'", soundProfile->name.c_str());
					if (ImGui::BeginTable("##sound_test_table", 3, tableFlags, { -1, 200 }))
					{

						ImGui::TableSetupScrollFreeze(0, 1);
						ImGui::TableSetupColumn("Name");
						ImGui::TableSetupColumn("Duration (sec)", ImGuiTableColumnFlags_WidthFixed);
						ImGui::TableSetupColumn("Play/Stop", ImGuiTableColumnFlags_WidthFixed);
						ImGui::TableHeadersRow();

						for (size_t i = 0; i < soundProfile->baseSounds.size(); i++)
						{
							Audio::SoundInstance& sound = soundProfile->baseSounds[i];

							ImGui::PushID(i);
							ImGui::TableNextRow(0, rowHeight);
							ImGui::TableNextColumn();

							float ratio = sound.getCurrentFrame() /
							              static_cast<float>(sound.getLengthInFrames());
							if (!sound.isPlaying())
								ratio = 0.0f;

							ImGui::ProgressBar(
							    ratio, { -1, 0 },
							    IO::formatString("%s_%02d",
							                     SE_NAMES[i % std::size(SE_NAMES)].data(),
							                     i / std::size(SE_NAMES) + 1)
							        .c_str());

							float durationSec,
							    durationMs = std::modf(sound.getDuration(), &durationSec);
							float timeSec,
							    timeMs = std::modf(
							        sound.isPlaying() ? sound.getCurrentTime() : 0.0f, &timeSec);
							ImGui::TableNextColumn();
							ImGui::Text("%02.f.%02.f/%02.f.%02.f", timeSec, timeMs * 100,
							            durationSec, durationMs * 100);

							ImGui::TableNextColumn();
							if (UI::transparentButton(ICON_FA_PLAY, UI::btnSmall))
							{
								sound.seek(0);
								sound.play();
							}

							ImGui::SameLine();
							ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
							ImGui::SameLine();
							if (UI::transparentButton(ICON_FA_STOP, UI::btnSmall))
								sound.stop();

							ImGui::PopID();
						}

						ImGui::EndTable();
					}
				}
				else
				{
					ImGui::Text("Sound effects not initialized!");
				}
			}

			ImGui::TreePop();
		}

		ImGui::End();
	}

	void SettingsWindow::open() { openPopup = true; }

	void SettingsWindow::updateGenericTab()
	{
		auto& config = getConfig();
		auto& style = ImGui::GetStyle();
		auto& tabName = localizeOrInsert("__settings_generic_tab", UI::itemLabel, Text::general);
		if (ImGui::BeginTabItem(tabName))
		{
			if (ImGui::CollapsingHeader(localize(Text::language), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				UI::labelPropertyColumn(localize(Text::language));

				ImGui::TableNextColumn();
				auto& languages = getLocalization().languages;
				ImGui::SetNextItemWidth(-UI::btnSmall.x - style.ItemSpacing.x);
				if (ImGui::BeginCombo("##language", localize(Text::languageName)))
				{
					const bool isAuto = config.language == "auto";
					if (ImGui::Selectable(localize(Text::automatic), isAuto))
						config.language = "auto";
					if (isAuto)
						ImGui::SetItemDefaultFocus();
					for (const auto& language : languages)
					{
						if (ImGui::Selectable(language.languageName.c_str(),
						                      language.code == config.language))
							config.language = language.code;
					}

					ImGui::EndCombo();
				}

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_SYNC, { UI::btnSmall.x, ImGui::GetFrameHeight() }))
					getLocalization().scanLanguages();

				UI::endPropertyTable();
			}

			// if (ImGui::CollapsingHeader(localize(Text::file),
			// ImGuiTreeNodeFlags_DefaultOpen))
			//{
			//	UI::beginPropertyColumns();
			//	UI::checkboxPropertyRow(localize(Text::minify), config.minifyOutput);
			//	UI::endPropertyColumns();
			// }

			if (ImGui::CollapsingHeader(localize(Text::autoSave), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				UI::checkboxPropertyRow(Text::autoSaveEnable, config.autoSaveEnabled);
				UI::intPropertyRow(Text::autoSaveInterval, config.autoSaveInterval);
				UI::intPropertyRow(Text::autoSaveCount, config.autoSaveMaxCount);
				UI::endPropertyTable();
			}

			if (ImGui::CollapsingHeader(localize(Text::theme), ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (UI::beginPropertyTable())
				{
					UI::selectPropertyRow(Text::baseTheme, config.baseTheme, baseThemeTexts,
					                      BaseTheme::BASE_THEME_MAX);
					UI::endPropertyTable();
				}

				ImGui::TextWrapped("%s", (const char*)localize(Text::accentColorHelp));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { style.ItemSpacing.x + 3, 15 });
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI::scale(1.5f));

				for (int i = 0; i < UI::accentColors.size(); ++i)
				{
					bool apply = false;
					std::string id = i == config.accentColor ? ICON_FA_CHECK : i == 0 ? "C" : "";
					ImGui::PushStyleColor(ImGuiCol_Button, UI::accentColors[i]);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::accentColors[i]);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, UI::accentColors[i]);

					const char* txt = i != config.accentColor ? i == 0 ? "C" : "" : ICON_FA_CHECK;
					ImGui::PushID(i);
					apply = ImGui::Button(txt, UI::btnNormal);
					ImGui::PopID();

					if (i != UI::accentColors.size() - 1)
						ImGui::SameLine();

					if (apply)
						config.accentColor = i;
				}
				ImGui::PopStyleColor(3 * UI::accentColors.size());
				ImGui::PopStyleVar(2);

				ImVec4& customColor = UI::accentColors[0];
				float col[]{ customColor.x, customColor.y, customColor.z };
				static ColorDisplay displayMode = ColorDisplay::HEX;
				constexpr ImGuiColorEditFlags colFlags[] = { ImGuiColorEditFlags_DisplayRGB,
					                                         ImGuiColorEditFlags_DisplayHSV,
					                                         ImGuiColorEditFlags_DisplayHex };

				ImGui::Separator();
				ImGui::TextWrapped("%s", (const char*)localize(Text::selectAccentColor));
				if (UI::beginPropertyTable())
				{
					int mode = static_cast<int>(displayMode);
					std::array<int, 3> modes = { 0, 1, 2 };
					if (UI::selectPropertyRow(Text::displayMode, mode, modes, 3, colorDisplayStr))
						displayMode = static_cast<ColorDisplay>(mode);

					UI::labelPropertyColumn(localize(Text::customColor));
					ImGuiColorEditFlags flags =
					    colFlags[(int)displayMode] | ImGuiColorEditFlags_Uint8 |
					    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoDragDrop;
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { style.ItemSpacing.x + 3, 15 });
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					if (ImGui::ColorEdit3("##custom_accent_color", col, flags))
					{
						customColor.x = std::clamp(col[0], 0.0f, 1.0f);
						customColor.y = std::clamp(col[1], 0.0f, 1.0f);
						customColor.z = std::clamp(col[2], 0.0f, 1.0f);
					}

					ImGui::PopStyleVar();
					UI::endPropertyTable();
				}
			}

			if (ImGui::CollapsingHeader(localize(Text::video), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				bool& vsync = Application::getInstance().getWindowState().vsync;
				if (UI::checkboxPropertyRow(Text::vsync, vsync))
					glfwSwapInterval(vsync ? 1 : 0);
				UI::endPropertyTable();
			}

			ImGui::EndTabItem();
		}
	}

	void SettingsWindow::updateTimelineTab(Audio::AudioManager& audio)
	{
		auto& config = getConfig();
		auto& style = ImGui::GetStyle();
		auto& tabName = localizeOrInsert("__settings_timeline_tab", UI::itemLabel, Text::timeline);
		if (ImGui::BeginTabItem(tabName))
		{
			if (ImGui::CollapsingHeader(localize(Text::timeline), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				UI::checkboxPropertyRow(Text::matchTimelineSizeToWindow,
				                        config.matchTimelineSizeToWindow);
				UI::checkboxPropertyRow(Text::matchNotesSizeToTimeline,
				                        config.matchNotesSizeToTimeline);
				ImGui::BeginDisabled(config.matchTimelineSizeToWindow);
				UI::intPropertyRow(Text::laneWidth, config.timelineWidth, "%dpx",
				                   ScoreEditorTimeline::MIN_LANE_WIDTH,
				                   ScoreEditorTimeline::MAX_LANE_WIDTH);
				ImGui::EndDisabled();
				ImGui::BeginDisabled(config.matchNotesSizeToTimeline);
				UI::intPropertyRow(Text::notesHeight, config.notesHeight, "%dpx",
				                   ScoreEditorTimeline::MIN_NOTES_HEIGHT,
				                   ScoreEditorTimeline::MAX_NOTES_HEIGHT);
				ImGui::EndDisabled();
				{
					UI::labelPropertyColumn(localize(Text::zoomSensitivity));

					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					ImGui::PushID(Text::zoomSensitivity.data());
					float scaledSensitivity = config.zoomSensitivity * 100;
					if (ImGui::SliderFloat("", &scaledSensitivity, 25, 400, "%.0f%%",
					                       ImGuiSliderFlags_AlwaysClamp |
					                           ImGuiSliderFlags_Logarithmic))
						config.zoomSensitivity = std::round(scaledSensitivity) / 100;
					ImGui::PopID();
				}

				UI::separatorRow();

				UI::checkboxPropertyRow(Text::hideStepOutlinesInPlayback,
				                        config.hideStepOutlinesInPlayback);
				UI::checkboxPropertyRow(Text::drawWaveform, config.drawWaveform);
				UI::checkboxPropertyRow(Text::returnToLastTick,
				                        config.returnToLastSelectedTickOnPause);
				UI::checkboxPropertyRow(Text::stopPlaybackAtMusicEnd,
				                        config.stopPlaybackAtMusicEnd);
				UI::checkboxPropertyRow(Text::cursorAutoScroll, config.followCursorInPlayback);
				UI::percentSliderPropertyRow(Text::cursorAutoScrollAmount,
				                             config.cursorPositionThreshold);
				UI::separatorRow();

				config.divisionType = std::clamp(config.divisionType, 0,
				                                 int(TimeDivisionType::DivisionTypeCount) - 1);
				TimeDivisionType divisionType = static_cast<TimeDivisionType>(config.divisionType);
				if (UI::selectPropertyRow(Text::divisonType, divisionType, timeDivisionTypeTexts))
					config.divisionType = static_cast<int>(divisionType);

				UI::endPropertyTable();
			}

			if (ImGui::CollapsingHeader(localize(Text::scrolling),
			                            ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				UI::floatPropertyRow(Text::scrollSpeedNormal, config.scrollSpeedNormal, "%.1fx",
				                     0.1f, 100.0f);
				UI::floatPropertyRow(Text::scrollSpeedShift, config.scrollSpeedShift, "%.1fx", 0.1f,
				                     100.0f);
				UI::separatorRow();

				UI::checkboxPropertyRow(Text::useSmoothScroll, config.useSmoothScrolling);
				UI::floatSliderPropertyRow(Text::smoothScrollTime, config.smoothScrollingTime,
				                           10.0f, 150.0f, "%.2fms");
				UI::endPropertyTable();
			}

			if (ImGui::CollapsingHeader(localize(Text::visuals), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				auto& profile = getResources().noteResources.getProfiles();
				if (UI::selectPropertyRow(Text::notesSkin, config.notesSkin, profile,
				                          profile.size(), profile.begin(),
				                          [](const std::string& s) { return s.c_str(); }))
					getResources().noteResources.load(config.notesSkin);
				UI::endPropertyTable();
			}

			if (ImGui::CollapsingHeader(localize(Text::audio), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				auto& profiles = audio.getSoundEffectsProfileNames();
				auto getPath = [](const auto& p) -> const std::string& { return p.first; };
				auto getName = [](const auto& p) -> const char* { return p.second.c_str(); };
				UI::selectPropertyRow(Text::notesSE, config.seProfilePath, profiles.begin(),
				                      profiles.end(), getPath, profiles.begin(), getName);
				auto profile = audio.getCurrentSoundEffectsProfile();
				if (profile && profile->credits.size() && ImGui::BeginItemTooltip())
				{
					ImGui::Text("%s", profile->credits.c_str());
					ImGui::EndTooltip();
				}
				UI::endPropertyTable();
			}

			if (ImGui::CollapsingHeader(localize(Text::background),
			                            ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				UI::checkboxPropertyRow(Text::drawBackground, config.drawBackground);

				std::string backgroundFile = config.backgroundImage;
				int result = UI::filePropertyRow(Text::backgroundImage, backgroundFile);
				if (result > 0)
				{
					config.backgroundImage = backgroundFile;
					isBackgroundChangePending = true;
				}
				else if (result < 0)
				{
					IO::FileDialog fileDialog{};
					fileDialog.title = "Open Image File";
					fileDialog.filters = { IO::imageFilter, IO::allFilter };
					fileDialog.parentWindowHandle = Application::getAppWindowHandle();

					if (fileDialog.openFile() == IO::FileDialogResult::OK)
					{
						config.backgroundImage = fileDialog.outputFilename;
						isBackgroundChangePending = true;
					}
				}

				UI::percentSliderPropertyRow(Text::backgroundBrightness,
				                             config.backgroundBrightness);
				UI::separatorRow();

				UI::percentSliderPropertyRow(Text::lanesOpacity, config.laneOpacity);
				UI::endPropertyTable();
			}

			if (ImGui::CollapsingHeader(localize(Text::advanced), ImGuiTreeNodeFlags_DefaultOpen) &&
			    UI::beginPropertyTable())
			{
				UI::checkboxPropertyRow(Text::showTickInProperties, config.showTickInProperties);
				UI::endPropertyTable();
			}

			ImGui::EndTabItem();
		}
	}

	void SettingsWindow::updateKeyConfigTab()
	{
		auto& input = getConfig().input;
		auto& style = ImGui::GetStyle();
		auto& tabName = localizeOrInsert("__settings_input_tab", UI::itemLabel, Text::keyConfig);
		if (ImGui::BeginTabItem(tabName))
		{
			ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg |
			                             ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollY |
			                             ImGuiTableFlags_NoSavedSettings |
			                             ImGuiTableFlags_SizingFixedFit;

			const ImGuiSelectableFlags selectionFlags =
			    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick;
			int rowHeight = ImGui::GetFrameHeight() + style.CellPadding.y;
			ImVec2 tableSize = ImGui::GetContentRegionAvail();
			tableSize.y -= 200;
			if (ImGui::BeginTable("commands_table", 2, tableFlags, tableSize))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn(localize(Text::action));
				ImGui::TableSetupColumn(localize(Text::keys), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (size_t i = 0; i < std::size(allInputBindings); ++i)
				{
					ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
					bool selected = i == selectedBindingIndex;
					MultiInputBinding& multiBinding = (input.*allInputBindings[i]);

					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(i);
					if (ImGui::Selectable(localize(multiBinding.name), selected, selectionFlags))
					{
						selectedBindingIndex = i;
						listeningForInput = false;
					}
					ImGui::PopID();

					ImGui::TableSetColumnIndex(1);
					if (multiBinding.count == 0)
						ImGui::Text(localize(Text::none));
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Separator,
						                      style.Colors[ImGuiCol_TableBorderStrong]);
						float width = (ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) /
						                  multiBinding.count -
						              style.ItemSpacing.x;
						for (int b = 0; b < multiBinding.count; b++)
						{
							const char* label = ToShortcutString(multiBinding.bindings[b]);
							if (strnlen(label, 1) == 0)
								label = localize(Text::none);
							float spacing = width - ImGui::CalcTextSize(label).x;
							ImGui::Text("%s", label);
							ImGui::SameLine(0, spacing);
							if (b + 1 != multiBinding.count)
							{
								ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
								ImGui::SameLine();
							}
						}
						ImGui::PopStyleColor();
					}
				}
				ImGui::EndTable();
			}
			ImGui::Separator();

			if (isArrayIndexInBounds(selectedBindingIndex, allInputBindings))
			{
				int b;
				int deleteBinding = -1;
				int moveIndex = -1;
				int moveDirection = 0;
				MultiInputBinding& multiBinding = (input.*allInputBindings[selectedBindingIndex]);

				ImGui::TextUnformatted(localize(multiBinding.name));

				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
				ImGui::BeginChild("##binding_keys_edit_window", { -1, -1 },
				                  ImGuiChildFlags_PaddedBorder);

				const float btnHeight = ImGui::GetFrameHeight();
				for (b = 0; b < multiBinding.count; ++b)
				{
					InputBinding& binding = multiBinding.bindings[b];
					ImGui::PushID(b);

					const char* label = ToShortcutString(binding);
					if (strnlen(label, 1) == 0)
						label = localize(Text::none);

					if (listeningForInput && editBindingIndex == b)
						label = localize(Text::cmdKeyListen);

					if (ImGui::Button(label, { -(btnHeight + style.ItemSpacing.x) * 3, btnHeight }))
					{
						listeningForInput = true;
						inputTimer.reset();
						editBindingIndex = b;
					}

					ImGui::SameLine();
					ImGui::BeginDisabled(b == 0);
					if (ImGui::Button(ICON_FA_CARET_UP, { btnHeight, btnHeight }))
					{
						moveIndex = b;
						moveDirection = -1;
					}
					ImGui::EndDisabled();

					ImGui::SameLine();
					ImGui::BeginDisabled(b + 1 == multiBinding.count);
					if (ImGui::Button(ICON_FA_CARET_DOWN, { btnHeight, btnHeight }))
					{
						moveIndex = b;
						moveDirection = 1;
					}
					ImGui::EndDisabled();

					ImGui::SameLine();
					if (ImGui::Button(ICON_FA_BAN, { btnHeight, btnHeight }))
						deleteBinding = b;

					ImGui::PopID();
				}
				if (multiBinding.count != multiBinding.bindings.size() &&
				    ImGui::Button(ICON_FA_PLUS, { -1, btnHeight }))
					multiBinding.addBinding({});

				ImGui::EndChild();
				ImGui::PopStyleColor();

				if (moveIndex >= 0)
				{
					listeningForInput = false;
					if (moveDirection < 0)
						multiBinding.moveUp(moveIndex);
					if (moveDirection > 0)
						multiBinding.moveDown(moveIndex);
				}

				if (deleteBinding > -1)
				{
					listeningForInput = false;
					multiBinding.removeAt(deleteBinding);
				}

				if (listeningForInput)
				{
					// ignore shortcuts while polling for keys
					ImGui::SetNextFrameWantCaptureKeyboard(true);
					if (inputTimer.elapsed() >= INPUT_TIMEOUT ||
					    ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						listeningForInput = false;
						editBindingIndex = -1;
					}
					else
					{
						InputBinding& binding = multiBinding.bindings[editBindingIndex];
						for (ImGuiKey key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END;
						     key = ImGuiKey(key + 1))
						{
							if (!ImGui::IsLRModKey(key) && ImGui::IsKeyDown(key))
							{
								binding.keyCode = key;
								binding.keyModifiers = ImGui::GetIO().KeyMods;
								listeningForInput = false;
								editBindingIndex = -1;
							}
						}
					}
				}
			}
			ImGui::EndTabItem();
		}
		else
		{
			// While input tab is not focus we clear out none keys
			for (size_t i = 0; i < std::size(allInputBindings); ++i)
			{
				MultiInputBinding& multiBinding = input.*allInputBindings[i];
				auto it = std::remove_if(multiBinding.bindings.begin(), multiBinding.bindings.end(),
				                         [](const InputBinding& inp) { return inp.keyCode == 0; });
				multiBinding.count = std::distance(multiBinding.bindings.begin(), it);
			}
		}
	}

	DialogResult SettingsWindow::update(Audio::AudioManager& audio)
	{
		auto fetchName = []() { return UI::modalTitle(Text::settings); };
		auto& windowName = localizeOrInsert("__settings_window", fetchName);
		if (openPopup == true)
		{
			ImGui::OpenPopup(windowName);
			openPopup = false;
		}

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), 0, { 0.5f, 0.5f });
		ImGui::SetNextWindowSize({ 750, 600 }, ImGuiCond_Appearing);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, UI::scale({ 20, 10 }));

		if (ImGui::BeginPopupModal(windowName, NULL, ImGuiWindowFlags_NoResize))
		{
			ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
			if (ImGui::BeginChild("##settings_panel", { -1, -UI::btnNormal.y - spacing.y }, 0,
			                      ImGuiWindowFlags_AlwaysVerticalScrollbar))
			{
				if (ImGui::BeginTabBar("##settings_tabs"))
				{
					updateGenericTab();
					updateTimelineTab(audio);
					updateKeyConfigTab();
					ImGui::EndTabBar();
				}
			}
			ImGui::EndChild();
			ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 100);
			if (ImGui::Button("OK", ImGui::GetContentRegionAvail()))
				ImGui::CloseCurrentPopup();

			ImGui::EndPopup();
		}

		ImGui::PopStyleVar();
		return DialogResult::None;
	}

	const char* LayersWindow::getWindowName()
	{
		auto fetchName = []()
		{
			return IO::formatString("%s %s###%s", ICON_FA_LAYER_GROUP, localize(Text::layer),
			                        "layers");
		};
		return localizeOrInsert("__layers_window", fetchName);
	}

	void LayersWindow::update(ScoreContext& context, GenericDialog& dialog)
	{
		bool openPopup = false;
		const bool dndActive = ImGui::IsDragDropActive();
		int swapFromIndex = -1;
		int swapToIndex = -1;
		int mergePattern = -1;
		int toggleHideIndex = -1;

		ImGuiStyle& style = ImGui::GetStyle();
		float buttonHeight = ImGui::GetFrameHeight();
		ImVec2 toolButtonSize = { UI::btnSmall.x, buttonHeight };
		float toolButtonsWidth = toolButtonSize.x * 4 + style.ItemSpacing.x * 6;
		float spacingHeight = dndActive ? UI::scale(8) : style.ItemSpacing.y;

		// Toggle show layer button
		UI::togglableButton(localize(Text::showAllLayers), { -1, buttonHeight },
		                    context.showAllLayers);
		// Layers window
		ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBg]);
		if (ImGui::BeginChild("layers_child_window",
		                      { 0, -ImGui::GetFrameHeightWithSpacing() - style.ItemSpacing.y * 3 },
		                      ImGuiChildFlags_PaddedBorder,
		                      ImGuiWindowFlags_AlwaysVerticalScrollbar))
		{
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, spacingHeight);
			for (id_t layerId = 0; layerId < context.score.layers.size(); layerId++)
			{
				auto& layer = context.score.layers[layerId];
				bool selected = layerId == context.selectedLayer;
				ImGui::PushID(layerId);
				// Layer button
				if (UI::togglableButton(layer.name.c_str(),
				                        { -toolButtonsWidth, ImGui::GetFrameHeight() }, selected))
					context.selectedLayer = layerId;
				UI::tooltip(localize(Text::layerOrderTooltip));
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_PayloadNoCrossProcess))
				{
					ImGui::SetDragDropPayload("DND_LAYER_MOVE", &layerId, sizeof(id_t));
					ImGui::Text("%s: %s", (const char*)localize(Text::layer), layer.name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload =
					        ImGui::AcceptDragDropPayload("DND_LAYER_MOVE"))
					{
						IM_ASSERT(payload->DataSize == sizeof(id_t));
						memcpy(&swapFromIndex, payload->Data, sizeof(id_t));
						swapToIndex = layerId * 2;
					}
					ImGui::EndDragDropTarget();
				}

				// Hide and unhide layer
				ImGui::SameLine();
				if (UI::transparentButton(layer.hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE,
				                          toolButtonSize))
					toggleHideIndex = layerId;
				UI::tooltip(localize(layer.hidden ? Text::layerShow : Text::layerHide));

				// Trigger layer edit
				ImGui::SameLine();
				if (UI::transparentButton(ICON_FA_PENCIL_ALT, toolButtonSize, false,
				                          context.metadata.isExtendedScore))
				{
					editIndex = layerId;
					editLayerName = layer.name;
					editForceNoteSpeed = isWithinRange(layer.forceNoteSpeed, 1, 12);
					editNoteSpeed = editForceNoteSpeed ? layer.forceNoteSpeed : 6.f;
					popupModalName = UI::modalTitle(Text::layerRename);
					openPopup = true;
				}
				UI::tooltip(localize(Text::layerRename));

				// Select all in the layer
				ImGui::SameLine();
				if (UI::transparentButton(ICON_FA_VECTOR_SQUARE, toolButtonSize))
					context.selectAll(layerId);
				UI::tooltip(localize(Text::selectAll));

				// Merge layer up
				ImGui::SameLine();
				if (UI::transparentButton(ICON_FA_LEVEL_UP_ALT, toolButtonSize, false,
				                          layerId != 0))
					mergePattern = layerId;
				UI::tooltip(localize(Text::layerMerge));

				ImGui::PopID();
			}

			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, buttonHeight);
			ImGui::SetCursorPosY(ImGui::GetCursorStartPos().y + buttonHeight);
			ImVec2 spacingSize = {
				std::max(ImGui::GetContentRegionAvail().x - toolButtonsWidth, 1.0f), spacingHeight
			};
			for (id_t layerId = 0; dndActive && layerId < context.score.layers.size(); layerId++)
			{
				ImGui::Dummy(spacingSize);
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload =
					        ImGui::AcceptDragDropPayload("DND_LAYER_MOVE"))
					{
						IM_ASSERT(payload->DataSize == sizeof(id_t));
						memcpy(&swapFromIndex, payload->Data, sizeof(id_t));
						swapToIndex = layerId * 2 + 1;
					}
					ImGui::EndDragDropTarget();
				}
			}
			ImGui::PopStyleVar(2);
			ImGui::EndChild();
		}
		ImGui::PopStyleColor();
		ImGui::Spacing();

		ImGui::BeginDisabled(!context.metadata.isExtendedScore);
		if (ImGui::Button(localize(Text::createLayer), { -1, ImGui::GetFrameHeightWithSpacing() }))
		{
			editIndex = -1;
			editLayerName = IO::formatString("#%d", context.score.layers.size());
			editForceNoteSpeed = false;
			editNoteSpeed = 6.f;
			popupModalName = UI::modalTitle(Text::createLayer);
			openPopup = true;
		}
		ImGui::EndDisabled();

		if (swapFromIndex >= 0 && swapToIndex >= 0)
		{
			if (swapToIndex % 2 == 0)
				doLayerSwap(context, swapFromIndex, swapToIndex / 2);
			else
				doLayerMove(context, swapFromIndex, (swapToIndex - swapFromIndex * 2) / 2);
		}

		if (mergePattern != -1)
		{
			if (!canLayerMerge(context.score, mergePattern))
			{
				std::string title = UI::modalTitle(Text::error);
				std::string content = localize(Text::layerMergeWarning).string;
				auto onYes = [&, index = mergePattern]() { doLayerMerge(context, index); };
				DialogContent::Action yes = { localize(Text::yes).string, onYes };
				DialogContent::Action no = { localize(Text::no).string, nullptr };
				dialog.open(title, { content }, { yes, no });
			}
			else
			{
				doLayerMerge(context, mergePattern);
			}
		}

		if (toggleHideIndex != -1)
		{
			doLayerHidden(context, toggleHideIndex);
		}

		if (openPopup)
			ImGui::OpenPopup(popupModalName.c_str());

		if (updateDialog() == DialogResult::Ok)
		{
			if (editIndex >= 0)
			{
				bool edited = false;
				LayerCapture before(context.score);
				Layer& layer = context.score.layers[editIndex];
				edited = layer.name != editLayerName ||
				         (isWithinRange(layer.forceNoteSpeed, 1, 12) != editForceNoteSpeed) ||
				         (editForceNoteSpeed && layer.forceNoteSpeed != editNoteSpeed);
				layer.name = editLayerName;
				layer.forceNoteSpeed = editForceNoteSpeed ? editNoteSpeed : 0.0f;
				editIndex = -1;
				if (edited)
					context.pushLayersEdit("Layer edit", std::move(before));
			}
			else
			{
				LayerCapture before(context.score);
				id_t layerId = static_cast<int>(context.score.layers.size());
				float noteSpeed = editForceNoteSpeed ? editNoteSpeed : 0.0f;
				context.score.layers.push_back(Layer{ layerId, editLayerName, noteSpeed });
				context.pushLayersEdit(localize(Text::createLayer), std::move(before));
			}
		}
	}

	DialogResult LayersWindow::updateDialog()
	{
		DialogResult result = DialogResult::None;

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always,
		                        { 0.5f, 0.5f });
		ImGui::SetNextWindowSize({ 450, 200 }, ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		if (ImGui::BeginPopupModal(popupModalName.c_str(), NULL, ImGuiWindowFlags_NoResize))
		{
			ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
			ImGui::Text("%s", (const char*)localize(Text::layerName));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			if (ImGui::InputText("##layer_name", &editLayerName,
			                     ImGuiInputTextFlags_EnterReturnsTrue) &&
			    editLayerName.size())
				result = DialogResult::Ok;
			ImGui::SetItemDefaultFocus();

			ImGui::Text("%s", (const char*)localize(Text::layerForceNoteSpeed));
			const char* enableText = localize(Text::enable);
			float textSpacing = 8;
			ImGui::SetNextItemWidth(-ImGui::GetFrameHeightWithSpacing() - textSpacing -
			                        ImGui::CalcTextSize(enableText).x);
			ImGui::BeginDisabled(!editForceNoteSpeed);
			ImGui::SliderFloat("###note_speed", &editNoteSpeed, 1, 12, "%.2f");
			ImGui::EndDisabled();
			ImGui::SameLine(0, textSpacing);
			ImGui::Text(enableText);
			ImGui::SameLine();
			ImGui::Checkbox("###force_note_speed", &editForceNoteSpeed);

			ImVec2 btnSz{ (ImGui::GetContentRegionAvail().x - spacing.x) / 2,
				          ImGui::GetFrameHeight() };
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y -
			                     ImGui::GetFrameHeight());
			if (ImGui::Button(localize(Text::cancel), btnSz))
				result = DialogResult::Cancel;

			ImGui::SameLine();
			ImGui::BeginDisabled(editLayerName.empty());
			if (ImGui::Button(localize(Text::confirm), btnSz))
				result = DialogResult::Ok;
			ImGui::EndDisabled();

			if (result != DialogResult::None)
				ImGui::CloseCurrentPopup();

			ImGui::EndPopup();
		}
		return result;
	}

	bool LayersWindow::canLayerMerge(const Score& score, id_t index)
	{
		auto& sourceHSC = score.layers[index - 1].hiSpeedChanges;
		auto& targetHSC = score.layers[index].hiSpeedChanges;
		auto itSrc = sourceHSC.begin(), endSrc = sourceHSC.end();
		auto itTrg = targetHSC.begin(), endTrg = targetHSC.end();
		while (itSrc != endSrc && itTrg != endTrg)
		{
			if (itSrc->first < itTrg->first)
				++itSrc;
			else
			{
				if (itSrc->first == itTrg->first && !isSame(itSrc->second, itTrg->second))
					return false;
				++itTrg;
			}
		}
		return true;
	}

	void LayersWindow::doLayerMerge(ScoreContext& context, id_t index)
	{
		LayerCapture before(context.score);
		for (auto& [_, note] : context.score.notes)
		{
			if (note.layer >= index)
				note.layer -= 1;
		}
		for (size_t i = index; i < context.score.layers.size(); i++)
		{
			for (auto&& [_, hispeed] : context.score.layers[i].hiSpeedChanges)
				hispeed.layer -= 1;
		}
		auto& sourceHSC = context.score.layers[index - 1].hiSpeedChanges;
		auto& targetHSC = context.score.layers[index].hiSpeedChanges;
		sourceHSC.merge(targetHSC);
		if (context.selectedLayer >= index)
			context.selectedLayer -= 1;
		context.score.layers.erase(context.score.layers.begin() + index);
		context.pushLayersEdit(localize(Text::layerMerge), std::move(before));
	}

	void LayersWindow::doLayerHidden(ScoreContext& context, id_t index)
	{
		LayerCapture before(context.score);
		auto& layer = context.score.layers.at(index);
		layer.hidden = !layer.hidden;
		context.pushLayersEdit(localize(layer.hidden ? Text::layerHide : Text::layerShow),
		                       std::move(before));
	}

	void LayersWindow::doLayerMove(ScoreContext& context, id_t index, id_t offset)
	{
		if (offset == 0)
			return;
		LayerCapture before(context.score);
		LayerCollection& layers = context.score.layers;
		if (offset > 0)
		{
			for (auto& [_, note] : context.score.notes)
			{
				if (note.layer == index)
					note.layer += offset;
				else if (note.layer > index && note.layer <= index + offset)
					note.layer -= 1;
			}
			auto begin = layers.begin() + index, mid = begin + 1, end = begin + offset + 1;
			for (auto it = mid; it != end; it++)
				for (auto&& [_, hispeed] : it->hiSpeedChanges)
					hispeed.layer -= 1;
			for (auto&& [_, hispeed] : begin->hiSpeedChanges)
				hispeed.layer += offset;
			std::rotate(begin, mid, end);
		}
		else
		{
			for (auto& [_, note] : context.score.notes)
			{
				if (note.layer == index)
					note.layer += offset;
				else if (note.layer < index && note.layer >= index + offset)
					note.layer += 1;
			}
			auto begin = layers.rbegin() + (layers.size() - index - 1), mid = begin + 1,
			     end = mid + std::abs(offset);
			for (auto it = mid; it != end; it++)
				for (auto&& [_, hispeed] : it->hiSpeedChanges)
					hispeed.layer += 1;
			for (auto&& [_, hispeed] : begin->hiSpeedChanges)
				hispeed.layer += offset;
			std::rotate(begin, mid, end);
		}
		if (index == context.selectedLayer)
			context.selectedLayer += offset;
		context.pushLayersEdit(localize(Text::layerChangeOrder), std::move(before));
	}

	void LayersWindow::doLayerSwap(ScoreContext& context, id_t index, id_t newIndex)
	{
		if (index == newIndex)
			return;
		LayerCapture before(context.score);
		for (auto& [_, note] : context.score.notes)
		{
			if (note.layer == index)
				note.layer = newIndex;
			else if (note.layer == newIndex)
				note.layer = index;
		}
		for (auto&& [_, hispeed] : context.score.layers[index].hiSpeedChanges)
			hispeed.layer = newIndex;
		for (auto&& [_, hispeed] : context.score.layers[newIndex].hiSpeedChanges)
			hispeed.layer = index;
		std::swap(context.score.layers[index], context.score.layers[newIndex]);
		if (index == context.selectedLayer)
			context.selectedLayer = newIndex;
		else if (newIndex == context.selectedLayer)
			context.selectedLayer = index;
		context.pushLayersEdit(localize(Text::layerChangeOrder), std::move(before));
	}

	const char* WaypointsWindow::getWindowName()
	{
		auto fetchName = []() { return UI::iconTitle(ICON_FA_LOCATION_ARROW, Text::waypoints); };
		return localizeOrInsert("__waypoints_window", fetchName);
	}

	void WaypointsWindow::update(ScoreEditorTimeline& timeline)
	{
		Score& score = timeline.context.score;
		if (UI::beginPropertyTable())
		{
			UI::labelPropertyColumn(localize(Text::gotoMeasure));

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-UI::btnSmall.x - ImGui::GetStyle().ItemSpacing.x * 2);
			ImGui::InputInt("##goto_measure", &gotoMeasure, 0, 0,
			                ImGuiInputTextFlags_AutoSelectAll);
			bool activateGoto = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter);

			ImGui::SameLine();
			activateGoto |= UI::transparentButton(ICON_FA_ARROW_RIGHT, UI::btnSmall);
			if (activateGoto)
			{
				const auto& score = timeline.context.score;
				gotoMeasure = std::max(std::min(gotoMeasure, MAX_MEASURE), 0);
				tick_t gotoTick = accumulateTicks(gotoMeasure, score.timeSignatures);
				timeline.scrollTo(accumulateDuration(gotoTick, score.tempoChanges), 0.5f);
			}

			UI::endPropertyTable();
		}

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImVec2 btnSize = { -1, ImGui::GetFrameHeight() };
		if (ImGui::BeginChild("child_window", { 0, -ImGui::GetFrameHeightWithSpacing() * 3 },
		                      ImGuiChildFlags_PaddedBorder))
		{
			auto wpIt = score.waypoints.begin(), wpEnd = score.waypoints.end();
			auto skIt = score.skills.begin(), skEnd = score.skills.end();
			std::pair<tick_t, const char*> fevers[2] = { { INT32_MAX, 0 }, { INT32_MAX, 0 } };
			auto fvIt = std::begin(fevers), fvEnd = fvIt;
			if (score.fever.startTick >= 0)
				*(fvEnd++) = std::make_pair(score.fever.startTick, "FEVER" ICON_FA_CARET_DOWN);
			if (score.fever.endTick >= 0)
				*(fvEnd++) = std::make_pair(score.fever.endTick, "FEVER" ICON_FA_CARET_UP);
			if (fevers[0].first > fevers[1].first)
				// this is bad but we need to allow it for correctness
				fevers[0].swap(fevers[1]);
			for (id_t index = 0; wpIt != wpEnd || skIt != skEnd || fvIt != fvEnd; index++)
			{
				int decrementElement;
				tick_t currTick = INT32_MAX;
				const char* currName = nullptr;
				ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
				if (wpIt != wpEnd && wpIt->second.tick < currTick)
				{
					auto&& [_, waypoint] = *wpIt;
					currTick = waypoint.tick;
					currName = waypoint.name.c_str();
					decrementElement = 0;
				}
				if (skIt != skEnd && skIt->tick < currTick)
				{
					currTick = skIt->tick;
					currName = localize(Text::skill);
					color = ImGui::ColorConvertU32ToFloat4(skillColor);
					decrementElement = 1;
				}
				if (fvIt != fvEnd && fvIt->first < currTick)
				{
					currTick = fvIt->first;
					currName = fvIt->second;
					color = ImGui::ColorConvertU32ToFloat4(feverColor);
					decrementElement = 2;
				}
				switch (decrementElement)
				{
				case 0:
					++wpIt;
					break;
				case 1:
					++skIt;
					break;
				case 2:
					++fvIt;
					break;
				}
				ImGui::PushID(index);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				if (ImGui::Button(currName, btnSize))
					timeline.scrollTo(accumulateDuration(currTick, score.tempoChanges), 0.5f);
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		if (ImGui::Button(localize(Text::createWaypoint), btnSize))
		{
			tick_t currentTick = timeline.getCurrentTick();
			const auto& waypoint = timeline.context.insertWaypoint(currentTick, "New Waypoint");
			timeline.scrollTo(accumulateDuration(waypoint.tick, score.tempoChanges), 0.5f);
			timeline.openEvent(waypoint);
		}
		if (ImGui::Button(localize(Text::insertSkill), btnSize))
		{
			tick_t currentTick = timeline.getCurrentTick();
			timeline.context.insertSkill(currentTick);
		}
		ImVec2 halfBtnSize = {
			(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2,
			ImGui::GetFrameHeight()
		};
		if (ImGui::Button(localize(Text::setFeverStart), halfBtnSize))
		{
			tick_t currentTick = timeline.getCurrentTick();
			const Fever before = score.fever;
			score.fever.startTick = currentTick;
			if (before.startTick != currentTick)
				timeline.context.pushEdit("Set Fever Start", FeverEdit{ { before, score.fever } });
		}
		ImGui::SameLine();
		if (ImGui::Button(localize(Text::setFeverEnd), halfBtnSize))
		{
			tick_t currentTick = timeline.getCurrentTick();
			const Fever before = score.fever;
			score.fever.endTick = currentTick;
			if (before.endTick != currentTick)
				timeline.context.pushEdit("Set Fever End", FeverEdit{ { before, score.fever } });
		}
	}
}

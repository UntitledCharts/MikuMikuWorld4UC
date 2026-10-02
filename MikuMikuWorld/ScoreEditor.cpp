// Put httplib first otherwise the compiler will throw an error
#define CPPHTTPLIB_OPENSSL_SUPPORT 1
#define CPPHTTPLIB_ZLIB_SUPPORT 1
#include <cpp-httplib/httplib.h>

#include "Application.h"
#include "ScoreEditor.h"
#include "Constants.h"
#include "File.h"
#include "SUS.h"
#include "NativeScoreSerializer.h"
#include "UI.h"
#include "Text.h"
#include "Utilities.h"
#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace MikuMikuWorld
{
	ScoreEditor::ScoreEditor()
	{
		renderer = std::make_unique<Renderer>();

		const auto& config = getConfig();
		audio.initializeAudioEngine();
		audio.setMasterVolume(config.masterVolume);
		audio.setMusicVolume(config.bgmVolume);
		audio.setSoundEffectsVolume(config.seVolume);
		audio.loadSoundEffects();
		audio.setSoundEffectsProfilePath(config.seProfilePath);

		autoSavePath = Application::getInstance().getConfigPath("auto_save");
		autoSaveTimer.reset();
		create();
	}

	void ScoreEditor::fetchUpdate()
	{
		using namespace std::chrono;
		auto& config = getConfig();
		system_clock::time_point now = system_clock::now();
		int diff = duration_cast<minutes>(now - config.lastUpdateCheck).count();
		std::cout << "Last update check: " << diff << " minutes ago" << std::endl;
		if (diff > 60)
		{
			try
			{
				httplib::Client client("https://api.github.com");

				std::cout << "Fetching new update" << std::endl;
				auto res = client.Get("/repos/UntitledCharts/MikuMikuWorld4UC/releases/latest");
				if (!res)
				{
					std::cerr << "Failed to fetch latest update: client.Get failed" << std::endl;
					return;
				}
				std::cout << "Status: " << res->status << std::endl;
				if (res->status == 200)
				{
					auto parsed = nlohmann::json::parse(res->body);
					std::string tagName = parsed["tag_name"];
					config.latestFetchAppVersion = tagName.substr(1);
					config.lastUpdateCheck = now;
				}
			}
			catch (const std::exception& e)
			{
				std::cout << "Failed to fetch latest update: " << e.what() << std::endl;
			}
		}

		auto& instance = Application::getInstance();
		auto currentVersion = IO::split(instance.getAppVersion(), ".");
		auto latestVersion = IO::split(config.latestFetchAppVersion, ".");

		if (currentVersion.size() != latestVersion.size())
		{
			std::cout << "Assertion failed: number of version part don't match" << std::endl;
			return;
		}

		std::cout << "Current version: " << instance.getAppVersion() << std::endl;
		std::cout << "Latest version: " << config.latestFetchAppVersion << std::endl;

		for (int i = 0; i < currentVersion.size(); i++)
		{
			auto currentVersionPart = std::stoi(currentVersion[i]);
			auto latestVersionPart = std::stoi(latestVersion[i]);

			if (latestVersionPart > currentVersionPart)
			{
				std::cout << "Update available" << std::endl;
				std::string title = UI::modalTitle(Text::updateAvailable);
				std::string content = IO::formatString(localize(Text::updateAvailableDescription),
				                                       config.latestFetchAppVersion);
				DialogContent::Action yes = { localize(Text::yes).string,
					                          &ScoreEditor::openReleasePage };
				DialogContent::Action no = { localize(Text::no).string, nullptr };
				dialog.open(title, { content }, { yes, no });
				return;
			}
		}

		std::cout << "No update" << std::endl;
	}

	void ScoreEditor::loadPresets(const FilePath& path) { presetManager.loadPresets(path); }
	void ScoreEditor::savePresets(const FilePath& path) { presetManager.savePresets(path); }

	void ScoreEditor::writeSettings()
	{
		auto& config = getConfig();
		config.masterVolume = audio.getMasterVolume();
		config.bgmVolume = audio.getMusicVolume();
		config.seVolume = audio.getSoundEffectsVolume();
	}

	void ScoreEditor::uninitialize() { audio.uninitializeAudioEngine(); }

	void ScoreEditor::update()
	{
		drawMenubar();
		auto& config = getConfig();
		auto timelineIt = timelines.find(currTimelineId);
		currTimeline = timelineIt != timelines.end() ? &timelineIt->second : nullptr;
		currContext = currTimeline ? &currTimeline->context : nullptr;
		if (currTimeline)
		{
			config.division = currTimeline->getQuarterDivision();
			config.zoom = currTimeline->getZoom().y;
		}
		toolbar.update(state, currContext, edit, pasteData);

		handleEvents();

		if (settingsWindow.isBackgroundChangePending)
		{
			getResources().backgroundResources.setBackground(config.backgroundImage);
			for (auto& [_, timeline] : timelines)
				timeline.getBackground().setDirty();
			settingsWindow.isBackgroundChangePending = false;
		}

		if (config.seProfilePath != audio.getCurrentProfilePath())
		{
			// audio.stopSoundEffects(false);
			audio.setSoundEffectsProfilePath(config.seProfilePath);
		}

		if (config.autoSaveEnabled && autoSaveTimer.elapsedMinutes() >= config.autoSaveInterval)
		{
			autoSave();
			autoSaveTimer.reset();
		}

		settingsWindow.update(audio);
		dialog.update();
		serializeWindow.update(*this);
		if (getConfig().debugEnabled)
			debugWindow.update(audio);

		ImGuiWindow* dockSpace = ImGui::FindWindowByName("InvisibleWindow");
		ImGuiID dockSpaceId = dockSpace ? dockSpace->GetID("InvisibleWindowDockSpace") : 0;
		constexpr auto timelineFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
		                               ImGuiWindowFlags_NoScrollWithMouse;
		for (auto&& [id, timeline] : timelines)
		{
			bool open = true;
			if (dockSpace)
				ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_Once);
			auto extraFlags = timeline.context.upToDate ? 0 : ImGuiWindowFlags_UnsavedDocument;
			if (ImGui::Begin(timeline.getWindowName(), &open, timelineFlags | extraFlags))
			{
				if (ImGui::IsWindowFocused())
				{
					if (currTimelineId != id)
					{
						ScoreContext& newContext = timelines.at(id).context;
						newContext.selectedFlag =
						    setFlag(newContext.selectedFlag, SelectionFlag::DirtyProperty);
					}
					currTimelineId = id;
				}

				timeline.update(edit, pasteData, renderer.get());
			}
			else
			{
				timeline.updateInBackground();
			}
			ImGui::End();

			if (!open)
				state.pendingCloseTimelines.push(id);
		}

		constexpr auto propWindowFlags =
		    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
		auto updateWindowNoContext = []()
		{ ImGui::TextDisabled(localize(Text::timelineNoContextSelected)); };
		if (ImGui::Begin(ScorePropertiesWindow::getWindowName(), NULL, propWindowFlags))
		{
			if (currContext)
				propertiesWindow.update(*currTimeline, *currContext, audio, dialog);
			else
				updateWindowNoContext();
		}
		ImGui::End();

		if (ImGui::Begin(ScoreNotePropertiesWindow::getWindowName(), NULL, propWindowFlags))
		{
			if (currContext)
				notePropertiesWindow.update(*currContext);
			else
				updateWindowNoContext();
		}
		ImGui::End();

		if (ImGui::Begin(ScoreOptionsWindow::getWindowName(), NULL, propWindowFlags))
		{
			if (currContext)
				optionsWindow.update(*currContext, edit);
			else
				updateWindowNoContext();
		}
		ImGui::End();

		if (ImGui::Begin(PresetsWindow::getWindowName(), NULL, propWindowFlags))
		{
			if (currContext)
				presetsWindow.update(presetManager, *currContext, pasteData);
			else
				updateWindowNoContext();
		}
		ImGui::End();

		if (ImGui::Begin(LayersWindow::getWindowName(), NULL, propWindowFlags))
		{
			if (currContext)
				layersWindow.update(*currContext, dialog);
			else
				updateWindowNoContext();
		}
		ImGui::End();

		if (ImGui::Begin(WaypointsWindow::getWindowName(), NULL, propWindowFlags))
		{
			if (currTimeline)
				waypointsWindow.update(*currTimeline);
			else
				updateWindowNoContext();
		}
		ImGui::End();

#ifdef DEBUG
		if (showImGuiDemoWindow)
			ImGui::ShowDemoWindow(&showImGuiDemoWindow);
#endif
	}

	void ScoreEditor::handleEvents()
	{
		auto& input = getConfig().input;
		ImGuiInputFlags defaultFlags = ImGuiInputFlags_RouteGlobal;
		if (!ImGui::GetIO().WantCaptureKeyboard)
		{
			if (ImGui::AnyShortcut(input.create, defaultFlags))
				state.wantCreateScore = true;
			if (ImGui::AnyShortcut(input.open, defaultFlags))
				state.wantOpenScore = true;
			if (ImGui::AnyShortcut(input.save, defaultFlags))
				state.wantSaveScore = true;
			if (ImGui::AnyShortcut(input.saveAs, defaultFlags))
				state.wantSaveAsScore = true;
			if (ImGui::AnyShortcut(input.exportScore, defaultFlags))
				state.wantExportScore = true;
			if (ImGui::AnyShortcut(input.openSettings, defaultFlags))
				settingsWindow.open();
			if (ImGui::AnyShortcut(input.openHelp, defaultFlags))
				openHelp();
			for (auto&& mode : EnumRange(InsertMode::InsertModeMax))
				if (ImGui::AnyShortcut(input.*insertModeBindings[int(mode)], defaultFlags))
					edit.changeInsertMode(mode);

			if (currTimeline && currContext)
			{
				float minNoteWidth = currContext->minNoteWidth(),
				      maxNoteWidth = currContext->maxNoteWidth();
				if (ImGui::AnyShortcut(input.togglePlayback, defaultFlags))
					currTimeline->setPlaying(!currTimeline->isPlaying());
				if (ImGui::AnyShortcut(input.stop, defaultFlags))
					currTimeline->stop();
				if (ImGui::AnyShortcut(input.previousTick, defaultFlags | ImGuiInputFlags_Repeat))
					currTimeline->jumpToPrevDivision();
				if (ImGui::AnyShortcut(input.nextTick, defaultFlags | ImGuiInputFlags_Repeat))
					currTimeline->jumpToNextDivision();
				if (ImGui::AnyShortcut(input.zoomOut, defaultFlags | ImGuiInputFlags_Repeat))
					currTimeline->setZoomY(currTimeline->getZoom().y /
					                       ScoreEditorTimeline::ZOOM_Y_FACTOR);
				if (ImGui::AnyShortcut(input.zoomIn, defaultFlags | ImGuiInputFlags_Repeat))
					currTimeline->setZoomY(currTimeline->getZoom().y *
					                       ScoreEditorTimeline::ZOOM_Y_FACTOR);
				if (ImGui::AnyShortcut(input.decreaseNoteSize,
				                       defaultFlags | ImGuiInputFlags_Repeat))
					edit.noteWidth = std::clamp(edit.noteWidth - 1, minNoteWidth, maxNoteWidth);
				if (ImGui::AnyShortcut(input.increaseNoteSize,
				                       defaultFlags | ImGuiInputFlags_Repeat))
					edit.noteWidth = std::clamp(edit.noteWidth + 1, minNoteWidth, maxNoteWidth);
				if (ImGui::AnyShortcut(input.undo, defaultFlags) && currContext->history.hasUndo())
					currContext->undo();
				if (ImGui::AnyShortcut(input.redo, defaultFlags) && currContext->history.hasRedo())
					currContext->redo();
				if (ImGui::AnyShortcut(input.selectAll, defaultFlags))
					currContext->selectAll();
				if (ImGui::AnyShortcut(input.deleteSelection, defaultFlags))
					currContext->deleteSelection();
				if (ImGui::AnyShortcut(input.cutSelection, defaultFlags))
					currContext->cutSelection();
				if (ImGui::AnyShortcut(input.copySelection, defaultFlags))
					currContext->copySelection();
				if (ImGui::AnyShortcut(input.paste, defaultFlags))
					pasteData.startPaste();
				if (ImGui::AnyShortcut(input.flipPaste, defaultFlags))
				{
					pasteData.startPaste();
					pasteData.flip();
				}
				if (ImGui::AnyShortcut(input.cancelPaste, defaultFlags))
					pasteData.cancelPaste();
				if (!currContext->selection.empty() &&
				    ImGui::AnyShortcut(input.duplicate, defaultFlags))
				{
					currContext->copySelection();
					pasteData.startPaste();
				}
				if (!currContext->selection.empty() &&
				    ImGui::AnyShortcut(input.flipDuplicate, defaultFlags))
				{
					currContext->copySelection();
					pasteData.startPaste();
					pasteData.flip();
				}
				if (ImGui::AnyShortcut(input.flip, defaultFlags))
					currContext->flipSelection();
				if (ImGui::IsAnyPressed(input.shrinkDown, defaultFlags))
					currContext->shrinkSelection(1);
				if (ImGui::IsAnyPressed(input.shrinkUp, defaultFlags))
					currContext->shrinkSelection(-1);
				if (ImGui::IsAnyPressed(input.compressSelection, defaultFlags))
					currContext->compressSelection();
				if (ImGui::IsAnyPressed(input.connectHolds, defaultFlags))
					currContext->connectHoldsInSelection();
				if (ImGui::IsAnyPressed(input.splitHold, defaultFlags))
					currContext->splitHoldInSelection();
				if (ImGui::IsAnyPressed(input.lerpHiSpeeds, defaultFlags))
					currContext->lerpHiSpeeds(currTimeline->getQuarterDivision(), EaseType::Linear);
				if (ImGui::IsAnyPressed(input.addTracesForHold, defaultFlags))
					currContext->convertHoldToTraces(currTimeline->getQuarterDivision(), false);
				if (ImGui::IsAnyPressed(input.convertHoldToTraces, defaultFlags))
					currContext->convertHoldToTraces(currTimeline->getQuarterDivision(), true);
				if (ImGui::IsAnyPressed(input.convertHoldToGuide, defaultFlags))
					currContext->convertHoldToGuide(edit.colorType);
				if (ImGui::IsAnyPressed(input.convertGuideToHold, defaultFlags))
					currContext->convertGuideToHold(edit.insertMode == InsertMode::MakeCritical);
				if (ImGui::IsAnyPressed(input.convertHoldToNone, defaultFlags))
					currContext->convertHoldToNone();
			}
		}

		if (state.wantCreateScore)
		{
			create();
			state.wantCreateScore = false;
		}
		if (state.wantOpenScore)
		{
			open();
			state.wantOpenScore = false;
		}
		if (state.wantExportScore)
		{
			if (currTimeline)
			{
				exportScore(*currTimeline);
			}
			state.wantExportScore = false;
		}
		if (state.wantSaveScore)
		{
			if (currTimeline)
			{
				if (currTimeline->context.filename.empty())
					saveAs(*currTimeline);
				else
					currTimeline->saveScore(currTimeline->context.filename);
			}
			state.wantSaveScore = false;
		}
		if (state.wantSaveAsScore)
		{
			if (currTimeline)
				saveAs(*currTimeline);
			state.wantSaveAsScore = false;
		}
		if (state.wantClosing)
		{
			if (state.closingTimelines == 0 && timelines.size())
			{
				for (auto&& [id, _] : timelines)
					state.pendingCloseTimelines.push(id);
				state.wantClosing = false;
			}
		}
		if (!serializeWindow.isSerializing() && state.pendingOpenFiles.size())
		{
			const auto& filepath = state.pendingOpenFiles.front();
			std::string filename = IO::toString(filepath);
			std::string extension = IO::toString(IO::File::getFileExtension(filepath));
			std::transform(extension.begin(), extension.end(), extension.begin(), tolower);
			if (currContext && Audio::isSupportedFileFormat(extension))
			{
				currContext->isPendingLoadMusic = true;
				currContext->workingMetadata.musicFile = filename;
				currContext->pushWorkingMetadata(&ScoreMetadata::musicFile, "Change score music", true);
			}
			else
			{
				id_t timelineID = currTimelineId;
				if (!currContext || currContext->score.notes.size())
				{
					timelineID = nextTimelineId;
					create();
				}
				serializeWindow.deserialize(timelines.at(timelineID), filename);
			}
			state.pendingOpenFiles.pop();
		}
		while (state.pendingCloseTimelines.size())
		{
			id_t id = state.pendingCloseTimelines.front();
			auto timelineIt = timelines.find(id);
			if (timelineIt == timelines.end())
			{
				state.pendingCloseTimelines.pop();
				continue;
			}
			if (!timelineIt->second.context.upToDate)
			{
				std::string title = UI::modalTitle(Text::unsavedChanges);

				auto onSave = [this, &timeline = timelineIt->second, ID = id]()
				{
					state.closingTimelines--;
					if (timeline.context.filename.empty())
						saveAs(timeline);
					else
						timeline.saveScore(timeline.context.filename);
					timelines.erase(ID);
					if (currTimelineId == ID)
					{
						currContext = nullptr;
						currTimeline = nullptr;
					}
				};
				auto onDiscard = [this, ID = id]()
				{
					state.closingTimelines--;
					timelines.erase(ID);
					if (currTimelineId == ID)
					{
						currContext = nullptr;
						currTimeline = nullptr;
					}
				};
				auto onCancel = [this]()
				{
					state.closingTimelines--;
					state.wantClosing = false;
					WindowState& appState = Application::getInstance().getWindowState();
					if (appState.closing)
						appState.closing = false;
				};
				DialogContent::Action save = { localize(Text::saveChanges).string, onSave };
				DialogContent::Action discard = { localize(Text::discardChanges).string,
					                              onDiscard };
				DialogContent::Action cancel = { localize(Text::cancel).string, onCancel };
				state.closingTimelines++;
				dialog.open(title,
				            { localize(Text::askSave).string, localize(Text::warnUnsaved).string },
				            { save, discard, cancel });
			}
			else
			{
				timelines.erase(timelineIt);
				if (currTimelineId == id)
				{
					currContext = nullptr;
					currTimeline = nullptr;
				}
			}
			state.pendingCloseTimelines.pop();
		}
	}

	size_t ScoreEditor::updateRecentFilesList(const std::string& entry)
	{
		auto& files = getConfig().recentFiles;
		while (files.size() >= maxRecentFilesEntries)
			files.pop_back();

		// Remove the entry (if found) to the beginning of the vector
		auto it = std::find(files.begin(), files.end(), entry);
		if (it != files.end())
			files.erase(it);

		files.insert(files.begin(), entry);
		return files.size();
	}

	void ScoreEditor::create()
	{
		currTimelineId = nextTimelineId++;
		auto&& timelineIt = timelines.emplace_hint(timelines.end(), currTimelineId,
		                                           ScoreEditorTimeline(currTimelineId, audio));
		const auto& config = getConfig();
		timelineIt->second.setQuarterDivision(config.division);
		timelineIt->second.setZoomY(config.zoom);
	}

	void ScoreEditor::loadScore(std::string filename)
	{
		if (!IO::File::exists(IO::stringToPath(filename)))
			return;

		for (auto& [_, timeline] : timelines)
			timeline.setPlaying(false);
		state.pendingOpenFiles.push(IO::stringToPath(filename));
	}

	void ScoreEditor::loadMusic(std::string filename)
	{
		if (currContext)
		{
			currContext->isPendingLoadMusic = true;
			currContext->workingMetadata.musicFile = std::move(filename);
			currContext->pushWorkingMetadata(&ScoreMetadata::musicFile, "Change score music", true);
		}
	}

	void ScoreEditor::open()
	{
		IO::FileDialog fileDialog{};
		fileDialog.parentWindowHandle = Application::getAppWindowHandle();
		fileDialog.title = "Open Score File";
		fileDialog.filters = { IO::combineFilters("All Supported Files",
			                                      { IO::mmwsFilter, IO::susFilter, IO::uscFilter,
			                                        IO::lvlDatFilter }),
			                   IO::mmwsFilter,
			                   IO::susFilter,
			                   IO::uscFilter,
			                   IO::lvlDatFilter,
			                   IO::allFilter };

		if (fileDialog.openFile() == IO::FileDialogResult::OK)
			loadScore(fileDialog.outputFilename);
	}

	bool ScoreEditor::close()
	{
		if (state.wantClosing)
			return state.closingTimelines == 0 && timelines.empty();
		state.wantClosing = true;
		return false;
	}

	bool ScoreEditor::saveAs(ScoreEditorTimeline& timeline)
	{
		IO::FileDialog fileDialog{};
		fileDialog.title = "Save Chart";
		fileDialog.filters = { IO::mmwsNativeFilter };
		fileDialog.defaultExtension =
		    ScoreSerializeController::getFormatDefaultExtension(SerializeFormat::NativeFormat)
		        .c_str();
		fileDialog.parentWindowHandle = Application::getAppWindowHandle();
		fileDialog.inputFilename = IO::toString(
		    IO::File::getFilenameWithoutExtension(IO::stringToPath(timeline.context.filename)));

		if (fileDialog.saveFile() == IO::FileDialogResult::OK)
		{
			timeline.context.filename = fileDialog.outputFilename;
			bool saved = timeline.saveScore(currTimeline->context.filename);
			if (saved)
				updateRecentFilesList(fileDialog.outputFilename);

			return saved;
		}

		return false;
	}

	void ScoreEditor::exportScore(ScoreEditorTimeline& timeline)
	{
		SerializeFormat format = static_cast<SerializeFormat>(getConfig().defaultExportFormat);
		if (ScoreSerializeController::isValidFormat(format))
		{
			IO::FileDialog fileDialog{};
			fileDialog.title = "Export Chart";
			fileDialog.filters = { ScoreSerializeController::getFormatFilter(format) };
			fileDialog.defaultExtension =
			    ScoreSerializeController::getFormatDefaultExtension(format);
			fileDialog.parentWindowHandle = Application::getAppWindowHandle();

			if (fileDialog.saveFile() == IO::FileDialogResult::OK)
				serializeWindow.serialize(timeline, fileDialog.outputFilename);
		}
		else
		{
			serializeWindow.serialize(timeline);
		}
	}

	void ScoreEditor::drawMenubar()
	{
		ImGui::BeginMainMenuBar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, UI::scale({ 10, 2 }));

		auto& config = getConfig();
		auto& input = config.input;
		if (ImGui::BeginMenu(localize(Text::file)))
		{
			if (ImGui::MenuItem(localize(Text::newFile), ToShortcutString(input.create)))
				state.wantCreateScore = true;

			if (ImGui::MenuItem(localize(Text::openFile), ToShortcutString(input.open)))
				state.wantOpenScore = true;

			if (ImGui::BeginMenu(localize(Text::openRecent)))
			{
				for (size_t index = 0; index < config.recentFiles.size(); index++)
				{
					const std::string& entry = config.recentFiles[index];
					// avoid ID collision
					std::string label = IO::formatString("%s##recent_items_%zd", entry, index);
					if (ImGui::MenuItem(label.c_str()))
					{
						FilePath path = IO::stringToPath(entry);
						if (IO::File::exists(path))
						{
							appendOpenFile(path);
						}
						else
						{
							std::string title = UI::modalTitle(Text::fileNotFound);
							std::string content = IO::formatString(
							    "%s \"%s\" %s.\n%s", localize(Text::fileNotFoundMsg1), entry,
							    localize(Text::fileNotFoundMsg2),
							    localize(Text::removeRecentFileNotFound));
							auto onYes = [index = index]()
							{
								auto& config = getConfig();
								if (!isArrayIndexInBounds(index, config.recentFiles))
									return;
								config.recentFiles.erase(config.recentFiles.begin() + index);
							};
							DialogContent::Action yes = { localize(Text::yes).string, onYes };
							DialogContent::Action no = { localize(Text::no).string, nullptr };
							dialog.open(title, { content }, { yes, no });
						}
					}
				}

				ImGui::Separator();
				bool hasRecentFiles = config.recentFiles.size();
				if (ImGui::MenuItem(localize(Text::clear), nullptr, false, hasRecentFiles))
					config.recentFiles.clear();

				ImGui::EndMenu();
			}

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::save), ToShortcutString(input.save), false,
			                    currContext))
				state.wantSaveScore = true;

			if (ImGui::MenuItem(localize(Text::saveAs), ToShortcutString(input.saveAs), false,
			                    currContext))
				state.wantSaveAsScore = true;

			if (ImGui::MenuItem(localize(Text::exportScore), ToShortcutString(input.exportScore),
			                    false, currContext))
				state.wantExportScore = true;

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::exit), ToShortcutString(ImGuiKey_F4, ImGuiMod_Alt)))
				Application::getInstance().getWindowState().closing = true;

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(localize(Text::edit)))
		{
			if (ImGui::MenuItem(localize(Text::undo), ToShortcutString(input.undo), false,
			                    currContext && currContext->history.hasUndo()))
				currContext->undo();

			if (ImGui::MenuItem(localize(Text::redo), ToShortcutString(input.redo), false,
			                    currContext && currContext->history.hasRedo()))
				currContext->redo();

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::del), ToShortcutString(input.deleteSelection), false,
			                    currContext && !currContext->selection.empty()))
				currContext->deleteSelection();

			if (ImGui::MenuItem(localize(Text::cut), ToShortcutString(input.cutSelection), false,
			                    currContext && !currContext->selection.empty()))
				currContext->cutSelection();

			if (ImGui::MenuItem(localize(Text::copy), ToShortcutString(input.copySelection), false,
			                    currContext && !currContext->selection.empty()))
				currContext->copySelection();

			if (ImGui::MenuItem(localize(Text::paste), ToShortcutString(input.paste)))
				pasteData.startPaste();

			if (ImGui::MenuItem(localize(Text::duplicate), ToShortcutString(input.duplicate), false,
			                    currContext && !currContext->selection.empty()))
			{
				currContext->copySelection();
				pasteData.startPaste();
			}

			if (ImGui::MenuItem(localize(Text::flipDuplicate),
			                    ToShortcutString(input.flipDuplicate), false,
			                    currContext && !currContext->selection.empty()))
			{
				currContext->copySelection();
				pasteData.startPaste();
				pasteData.flip();
			}

			if (ImGui::MenuItem(localize(Text::flip), ToShortcutString(input.flip), false,
			                    currContext && !currContext->selection.empty()))
				currContext->flipSelection();

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::selectAll), ToShortcutString(input.selectAll)))
				currContext->selectAll();

			ImGui::Separator();
			if (ImGui::MenuItem(localize(Text::settings), ToShortcutString(input.openSettings)))
				settingsWindow.open();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(localize(Text::view)))
		{
			ImGui::MenuItem(localize(Text::showStepOutlines), NULL, &edit.drawHoldStepOutlines);
			ImGui::MenuItem(localize(Text::hideStepOutlinesInPlayback), NULL,
			                &config.hideStepOutlinesInPlayback);
			ImGui::MenuItem(localize(Text::cursorAutoScroll), NULL, &config.followCursorInPlayback);
			ImGui::MenuItem(localize(Text::returnToLastTick), NULL,
			                &config.returnToLastSelectedTickOnPause);
			ImGui::MenuItem(localize(Text::drawWaveform), NULL, &config.drawWaveform);
			ImGui::MenuItem(localize(Text::stopPlaybackAtMusicEnd), NULL,
			                &config.stopPlaybackAtMusicEnd);
			if (ImGui::MenuItem(localize(Text::autoSave), localize(Text::openFile)))
				openAutoSavePath();

			ImGui::EndMenu();
		}

		if (getConfig().debugEnabled)
		{
			if (ImGui::BeginMenu(localize(Text::debug)))
			{
#ifdef DEBUG
				ImGui::MenuItem("ImGui Demo Window", NULL, &showImGuiDemoWindow);
#endif

				if (ImGui::MenuItem("Auto Save"))
					autoSave();

				if (ImGui::MenuItem("Delete Old Auto Save (1)"))
					deleteOldAutoSave(1);

				if (ImGui::MenuItem("Delete Old Auto Save (Max)"))
					deleteOldAutoSave(config.autoSaveMaxCount);

				bool audioRunning = audio.isEngineStarted();
				if (ImGui::MenuItem(audioRunning ? "Stop Audio" : "Start Audio",
				                    audioRunning ? ICON_FA_VOLUME_UP : ICON_FA_VOLUME_MUTE))
				{
					if (audioRunning)
						audio.stopEngine();
					else
						audio.startEngine();
				}

				if (ImGui::MenuItem("Check updates"))
				{
					std::thread fetchUpdateThread(&ScoreEditor::fetchUpdate, this);
					fetchUpdateThread.detach();
				}

				ImGui::EndMenu();
			}
		}

		if (ImGui::BeginMenu(localize(Text::window)))
		{
			if (ImGui::MenuItem(localize(Text::vsync), NULL, &getConfig().vsync))
				glfwSwapInterval(getConfig().vsync);

			ImGui::MenuItem(localize(Text::fpsShow), NULL, &getConfig().showFPS);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(localize(Text::help)))
		{
			if (ImGui::MenuItem(localize(Text::help), ToShortcutString(input.openHelp)))
				openHelp();

			if (ImGui::MenuItem(localize(Text::about)))
			{
				std::string title = UI::modalTitle(Text::about);
				const char* desc =
				    APP_NAME "\nThis application is based on MikuMikuWorld for Chart Cyanvas\n"
				             "Copyright (c) 2023 Nanashi. (@sevenc-nanashi)\n\n"
				             "Which was based on MikuMikuWorld.\n"
				             "Copyright (C) 2022 Crash5b\n\n";
				std::string appVersion =
				    IO::formatString("Version %s", Application::getInstance().getAppVersion());
				std::string translator =
				    IO::formatString(localize(Text::translatedBy), localize(Text::translator));
				dialog.open(title, { desc, "", appVersion, translator }, { { "OK", nullptr } });
			}
			ImGui::EndMenu();
		}

		if (getConfig().showFPS)
		{
			std::string fps = IO::formatString("%.3fms (%.1fFPS)", ImGui::GetIO().DeltaTime * 1000,
			                                   ImGui::GetIO().Framerate);
			ImGui::SetCursorPosX(ImGui::GetWindowSize().x - ImGui::CalcTextSize(fps.c_str()).x -
			                     ImGui::GetStyle().WindowPadding.x);
			ImGui::Text(fps.c_str());
		}

		ImGui::PopStyleVar();
		ImGui::EndMainMenuBar();
	}

	void ScoreEditor::autoSave()
	{
		namespace fs = std::filesystem;
		// create auto save directory if none exists
		if (!fs::exists(autoSavePath))
			fs::create_directory(autoSavePath);

		for (auto&& [_, timeline] : timelines)
		{
			std::string_view name = timeline.getTimelineName();
			FilePath saveFile = IO::stringToPath(
			    IO::formatString("auto_save_%s-%.*s%s", Utilities::getCurrentDateTime(),
			                     name.size(), name.data(), UC_MMWS_EXTENSION));
			saveFile = autoSavePath / saveFile;
			NativeScoreSerializer().serialize({ timeline.context.score, timeline.context.metadata },
			                                  IO::toString(saveFile));
		}

		// get mmws files
		int mmwsCount = 0;
		for (const auto& file : fs::directory_iterator(autoSavePath))
		{
			std::string extension = IO::toString(file.path().extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			std::string filename = IO::toString(file.path().filename());
			mmwsCount +=
			    extension == UC_MMWS_EXTENSION && (IO::startsWith(filename, "mmw_auto_save_") ||
			                                       IO::startsWith(filename, "auto_save_"));
		}

		// delete older files
		if (mmwsCount > getConfig().autoSaveMaxCount)
			deleteOldAutoSave(mmwsCount - getConfig().autoSaveMaxCount);
	}

	int ScoreEditor::deleteOldAutoSave(int count) const
	{
		namespace fs = std::filesystem;
		if (!fs::exists(autoSavePath))
			return 0;

		// get mmws files
		using entry = fs::directory_entry;
		std::vector<entry> deleteFiles;
		for (const auto& file : fs::directory_iterator(autoSavePath))
		{
			std::string extension = IO::toString(file.path().extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			std::string filename = IO::toString(file.path().filename());
			if (extension == UC_MMWS_EXTENSION && (IO::startsWith(filename, "mmw_auto_save_") ||
			                                       IO::startsWith(filename, "auto_save_")))
				deleteFiles.push_back(file);
		}

		// sort files by modification date
		std::sort(deleteFiles.begin(), deleteFiles.end(), [](const entry& f1, const entry& f2)
		          { return f1.last_write_time() > f2.last_write_time(); });

		int deleteCount = 0;
		int remainingCount = count;
		while (remainingCount && deleteFiles.size())
		{
			fs::remove(deleteFiles.back());
			deleteFiles.pop_back();

			--remainingCount;
			++deleteCount;
		}

		return deleteCount;
	}

	void ScoreEditor::appendOpenFile(const FilePath& filepath)
	{
		state.pendingOpenFiles.push(filepath);
	}

	void ScoreEditor::openHelp()
	{
		ImGui::GetPlatformIO().Platform_OpenInShellFn(
		    GImGui, "https://github.com/crash5band/MikuMikuWorld/wiki");
	}

	void ScoreEditor::openReleasePage()
	{
		ImGui::GetPlatformIO().Platform_OpenInShellFn(
		    GImGui, "https://github.com/UntitledCharts/MikuMikuWorld4UC/releases");
	}

	void ScoreEditor::openAutoSavePath() const
	{
		ImGui::GetPlatformIO().Platform_OpenInShellFn(GImGui, IO::toString(autoSavePath).c_str());
	}
}

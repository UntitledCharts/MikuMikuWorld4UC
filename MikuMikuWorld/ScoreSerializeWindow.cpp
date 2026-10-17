#include "Application.h"
#include "ScoreSerializeWindow.h"

#include "NativeScoreSerializer.h"
#include "SusSerializer.h"
#include "UscSerializer.h"
#include "SonolusSerializer.h"

#include "Text.h"
#include "Colors.h"
#include "ScoreEditor.h"

namespace MikuMikuWorld
{
	static constexpr size_t FORMAT_COUNT = static_cast<size_t>(SerializeFormat::FormatCount);

	static const std::array<std::string, 5> FORMAT_NAMES = {
		IO::formatString("%s (%s)", IO::mmwsFilter.filterName, UC_MMWS_EXTENSION),
		IO::formatString("%s (%s)", IO::mmwsLegacyFilter.filterName, MMWS_EXTENSION),
		IO::formatString("%s (%s)", IO::susFilter.filterName, SUS_EXTENSION),
		IO::formatString("%s (%s)", IO::uscFilter.filterName, USC_EXTENSION),
		IO::lvlDatFilter.filterName,
	};
	static_assert(FORMAT_COUNT == FORMAT_NAMES.size());

	static constexpr std::array<size_t, 5> IMPORT_AVAILABILITY = { true, false, true, true, true };
	static_assert(FORMAT_COUNT == IMPORT_AVAILABILITY.size());
	static constexpr std::array<size_t, 5> EXPORT_AVAILABILITY = { false, true, true, true, true };
	static_assert(FORMAT_COUNT == EXPORT_AVAILABILITY.size());

	DefaultScoreSerializeController::DefaultScoreSerializeController(SerializingScore score)
	{
		this->score = std::move(score);

		serializable.resize(FORMAT_COUNT, Result::Ok());
		auto checkFormat = [this](SerializeFormat format, auto canSerialize)
		{
			if (!EXPORT_AVAILABILITY[(size_t)format])
				return;
			serializable[(size_t)format] = canSerialize(this->score);
		};
		checkFormat(SerializeFormat::NativeFormat, NativeScoreSerializer::canSerialize);
		checkFormat(SerializeFormat::LegacyNativeFormat, LegacyNativeScoreSerializer::canSerialize);
		checkFormat(SerializeFormat::SusFormat, SusSerializer::canSerialize);
		checkFormat(SerializeFormat::UscFormat, UscSerializer::canSerialize);
		checkFormat(SerializeFormat::LvlDataFormat, PySekaiEngine::canSerialize);
	}

	DefaultScoreSerializeController::DefaultScoreSerializeController(SerializingScore score,
	                                                                 const std::string& filename)
	    : DefaultScoreSerializeController(std::move(score))
	{
		this->filename = filename;
		createSerializer();
	}

	void DefaultScoreSerializeController::createSerializer()
	{
		SerializeFormat format = toSerializeFormat(filename);
		switch (format)
		{
		case SerializeFormat::NativeFormat:
			serializer = std::make_unique<NativeScoreSerializer>();
			break;
		case SerializeFormat::LegacyNativeFormat:
			serializer = std::make_unique<LegacyNativeScoreSerializer>();
			break;
		case SerializeFormat::SusFormat:
			serializer = std::make_unique<SusSerializer>();
			break;
		case SerializeFormat::UscFormat:
			serializer = std::make_unique<UscSerializer>(getConfig().minifyOutput);
			break;
		case SerializeFormat::LvlDataFormat:
			serializer =
			    std::make_unique<SonolusSerializer>(std::make_unique<PySekaiEngine>(), true, false);
			break;
		default:
			errorMessage = "No serializer found!";
			serializer.reset();
			return;
		}

		if (format == SerializeFormat::NativeFormat)
			name = filename;
	}

	SerializeResult DefaultScoreSerializeController::update()
	{
		if (serializer && !filename.empty())
		{
			try
			{
				serializer->serialize(score, filename);
				serializer.reset();
				return SerializeResult::SerializeSuccess;
			}
			catch (const std::exception& err)
			{
				errorMessage = IO::formatString("%s\n"
				                                "%s: %s",
				                                localize(Text::errorSaveScoreFile),
				                                localize(Text::error), err.what());
				return SerializeResult::Error;
			}
		}
		else if (filename.empty())
		{
			SerializeResult result = SerializeResult::None;
			if (!ImGui::IsPopupOpen("###serializer_picker"))
				ImGui::OpenPopup("###serializer_picker");

			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always,
			                        ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSizeConstraints({ 450, 300 }, { FLT_MAX, FLT_MAX });
			ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize / 1.5f, ImGuiCond_Always);
			ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
			if (ImGui::BeginPopupModal(APP_NAME "###serializer_picker", NULL,
			                           ImGuiWindowFlags_NoResize))
			{
				const ImGuiStyle& style = ImGui::GetStyle();
				ImGui::Text(localize(Text::exportAsFileFormat));
				// ImGui::Checkbox(localize(Text::minify), &getConfig().minifyOutput);
				ImVec2 avail = ImGui::GetContentRegionAvail();
				float btnWidth = avail.x / 2 - style.ItemSpacing.x * 2,
				      btnHeight = ImGui::GetFrameHeight();

				if (ImGui::BeginListBox("###score_format_list",
				                        { avail.x, avail.y - btnHeight - style.ItemSpacing.y * 2 }))
				{
					for (int i = 0; i < FORMAT_NAMES.size(); ++i)
					{
						if (!EXPORT_AVAILABILITY[i])
							continue;
						bool isSelected = (static_cast<int>(selectedFormat) == i);
						if (isSelected)
						{
							const ImVec4 color = UI::accentColors[getConfig().accentColor];
							const ImVec4 darkColor = generateDarkColor(color);
							const ImVec4 lightColor = generateHighlightColor(color);
							ImGui::PushStyleColor(ImGuiCol_Header, color);
							ImGui::PushStyleColor(ImGuiCol_HeaderHovered, darkColor);
							ImGui::PushStyleColor(ImGuiCol_HeaderActive, lightColor);
						}
						ImGui::BeginDisabled(!serializable[i].isOk());
						if (ImGui::Selectable(FORMAT_NAMES[i].data(), isSelected))
							selectedFormat = static_cast<SerializeFormat>(i);
						if (!serializable[i].isOk() &&
						    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
						    ImGui::BeginTooltip())
						{
							ImGui::TextColored(ImVec4(0.96f, 0.26f, 0.21f, 1.0f),
							                   serializable[i].getMessage().c_str());
							ImGui::EndTooltip();
						}
						ImGui::EndDisabled();
						if (isSelected)
						{
							ImGui::PopStyleColor(3);
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndListBox();
				}

				ImGui::BeginDisabled(!isValidFormat(selectedFormat) ||
				                     !EXPORT_AVAILABILITY[(int)selectedFormat]);
				if (ImGui::Button(localize(Text::select), { btnWidth, btnHeight }))
				{
					IO::FileDialog fileDialog{};
					fileDialog.title = "Export Chart";
					fileDialog.filters = { getFormatFilter(selectedFormat) };
					fileDialog.defaultExtension = getFormatDefaultExtension(selectedFormat);
					fileDialog.parentWindowHandle = Application::getAppWindowHandle();

					if (fileDialog.saveFile() == IO::FileDialogResult::OK)
					{
						this->filename = fileDialog.outputFilename;
						createSerializer();
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndDisabled();
				ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2);
				if (ImGui::Button(localize(Text::cancel), { btnWidth, btnHeight }))
				{
					ImGui::CloseCurrentPopup();
					result = SerializeResult::Cancel;
				}
				ImGui::EndPopup();
			}
			return result;
		}
		else
			// Has filename but no valid serializer
			return SerializeResult::Error;
	}

	DefaultScoreDeserializeController::DefaultScoreDeserializeController(
	    const std::string& filename)
	{
		this->filename = filename;
		SerializeFormat format = toSerializeFormat(filename);
		if (isValidFormat(format))
		{
			selectedFormat = format;
			createDeserializer();
		}
	}

	void DefaultScoreDeserializeController::createDeserializer()
	{
		switch (selectedFormat)
		{
		case SerializeFormat::NativeFormat:
		case SerializeFormat::LegacyNativeFormat:
			deserializer = std::make_unique<NativeScoreSerializer>();
			break;
		case SerializeFormat::SusFormat:
			deserializer = std::make_unique<SusSerializer>();
			break;
		case SerializeFormat::UscFormat:
			deserializer = std::make_unique<UscSerializer>();
			break;
		case SerializeFormat::LvlDataFormat:
			deserializer = std::make_unique<SonolusSerializer>(std::make_unique<PySekaiEngine>());
			break;
		default:
			deserializer.reset();
			filename.clear();
			errorMessage = "No deserializer found!";
			return;
		}

		if (selectedFormat == SerializeFormat::NativeFormat)
			this->name = filename;
	}

	SerializeResult DefaultScoreDeserializeController::update()
	{
		if (deserializer && !filename.empty())
		{
			try
			{
				score = deserializer->deserialize(filename);
				return SerializeResult::DeserializeSuccess;
			}
			catch (PartialScoreDeserializeError& partialError)
			{
				score = partialError.getResult();
				errorMessage = partialError.what();
				return SerializeResult::PartialDeserializeSuccess;
			}
			catch (std::exception& error)
			{
				errorMessage =
				    IO::formatString("%s\n"
				                     "%s: %s\n"
				                     "%s: %s",
				                     localize(Text::errorLoadScoreFile), localize(Text::scoreFile),
				                     filename.c_str(), localize(Text::error), error.what());
				return SerializeResult::Error;
			}
		}
		else if (!filename.empty())
		{
			SerializeResult result = SerializeResult::None;
			if (!ImGui::IsPopupOpen("###deserializer_picker"))
				ImGui::OpenPopup("###deserializer_picker");

			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always,
			                        ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSizeConstraints({ 450, 300 }, { FLT_MAX, FLT_MAX });
			ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize / 1.5f, ImGuiCond_Always);
			ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
			if (ImGui::BeginPopupModal(APP_NAME "###deserializer_picker", NULL,
			                           ImGuiWindowFlags_NoResize))
			{
				const ImGuiStyle& style = ImGui::GetStyle();
				ImGui::Text("%s '%s'\n%s", (const char*)localize(Text::unknownFileFormat),
				            filename.c_str(), (const char*)localize(Text::openAsFileFormat));
				ImVec2 avail = ImGui::GetContentRegionAvail();
				float btnWidth = avail.x / 2 - style.ItemSpacing.x * 2,
				      btnHeight = ImGui::GetFrameHeight();

				if (ImGui::BeginListBox("###score_format_list",
				                        { avail.x, avail.y - btnHeight - style.ItemSpacing.y * 2 }))
				{
					for (int i = 0; i < FORMAT_NAMES.size(); ++i)
					{
						if (!IMPORT_AVAILABILITY[i])
							continue;
						bool isSelected = (static_cast<int>(selectedFormat) == i);
						if (isSelected)
						{
							const ImVec4 color = UI::accentColors[getConfig().accentColor];
							const ImVec4 darkColor = generateDarkColor(color);
							const ImVec4 lightColor = generateHighlightColor(color);
							ImGui::PushStyleColor(ImGuiCol_Header, color);
							ImGui::PushStyleColor(ImGuiCol_HeaderHovered, darkColor);
							ImGui::PushStyleColor(ImGuiCol_HeaderActive, lightColor);
						}
						if (ImGui::Selectable(FORMAT_NAMES[i].data(), isSelected))
							selectedFormat = static_cast<SerializeFormat>(i);
						if (isSelected)
						{
							ImGui::PopStyleColor(3);
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndListBox();
				}

				ImGui::BeginDisabled(!isValidFormat(selectedFormat) ||
				                     !IMPORT_AVAILABILITY[(int)selectedFormat]);
				if (ImGui::Button(localize(Text::select), { btnWidth, btnHeight }))
				{
					createDeserializer();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2);
				if (ImGui::Button(localize(Text::cancel), { btnWidth, btnHeight }))
				{
					result = SerializeResult::Cancel;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			return result;
		}
		else
			return SerializeResult::Error;
	}

	void ScoreSerializeWindow::update(ScoreEditor& editor)
	{
		if (!controller)
			return;
		switch (controller->update())
		{
		case SerializeResult::PartialSerializeSuccess:
			IO::messageBox(APP_NAME, controller->getErrorMessage(), IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Warning, Application::getAppWindowHandle());
			controller.reset();
			break;
		case SerializeResult::SerializeSuccess:
			IO::messageBox(APP_NAME, IO::formatString(localize(Text::exportSuccessful)),
			               IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Information,
			               Application::getAppWindowHandle());
			controller.reset();
			break;
		case SerializeResult::PartialDeserializeSuccess:
			IO::messageBox(APP_NAME, controller->getErrorMessage(), IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Warning, Application::getAppWindowHandle());
			[[fallthrough]];
		case SerializeResult::DeserializeSuccess:
			timeline->loadScore(std::move(controller->getSerializeScore().score),
			                    std::move(controller->getSerializeScore().metadata),
			                    controller->getFilename());
			if (!controller->getFilename().empty())
				editor.updateRecentFilesList(controller->getFilename());
			controller.reset();
			break;
		default:
		case SerializeResult::Error:
			IO::messageBox(APP_NAME, controller->getErrorMessage(), IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Error, Application::getAppWindowHandle());
			controller.reset();
			break;
		case SerializeResult::Cancel:
			controller.reset();
			break;
		case SerializeResult::None:
			break;
		}
	}

	void ScoreSerializeWindow::serialize(ScoreEditorTimeline& timeline)
	{
		controller = std::make_unique<DefaultScoreSerializeController>(
		    SerializingScore{ timeline.context.score, timeline.context.metadata });
		this->timeline = &timeline;
	}

	void ScoreSerializeWindow::serialize(ScoreEditorTimeline& timeline, const std::string& filename)
	{
		controller = std::make_unique<DefaultScoreSerializeController>(
		    SerializingScore{ timeline.context.score, timeline.context.metadata }, filename);
		this->timeline = &timeline;
	}

	void ScoreSerializeWindow::deserialize(ScoreEditorTimeline& timeline,
	                                       const std::string& filename)
	{
		controller.reset();
		controller = std::make_unique<DefaultScoreDeserializeController>(filename);
		this->timeline = &timeline;
	}

	bool ScoreSerializeWindow::isSerializing() const { return static_cast<bool>(controller); }
}
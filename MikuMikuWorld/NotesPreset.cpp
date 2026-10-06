#include "Application.h"
#include "NotesPreset.h"
#include "File.h"
#include "IO.h"
#include "JsonIO.h"
#include "Utilities.h"
#include <execution>
#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace MikuMikuWorld
{
	NotesPreset::NotesPreset(id_t _id, std::string _name) : ID{ _id }, name{ _name } {}

	Result NotesPreset::read(const FilePath& filepath)
	{
		if (!std::filesystem::exists(filepath))
			return Result(ResultStatus::Error,
			              "The preset file '" + IO::toString(filepath) + "' does not exist.");

		std::ifstream file(filepath);

		file >> data;
		file.close();

		filename = IO::toString(IO::File::getFilenameWithoutExtension(filepath));
		if (data.find("name") != data.end())
			name = data["name"];

		if (data.find("description") != data.end())
			description = data["description"];

		if (is_paste_data_empty(data))
			return Result(ResultStatus::Warning,
			              "The preset '" + name + "' does not contain any notes data. Skipping...");

		return Result::Ok();
	}

	void NotesPreset::write(FilePath filepath)
	{
		int count = 1;
		auto baseFilename = IO::toString(IO::File::getFilenameWithoutExtension(filepath));

		while (IO::File::exists(filepath))
			filepath = IO::stringToPath(IO::formatString("%s(%d).json", baseFilename, count));

		data["name"] = name;
		data["description"] = description;

		std::ofstream file(filepath);

		file << data;
		file.flush();
		file.close();
	}

	void PresetManager::loadPresets(const FilePath& libPath)
	{
		if (!std::filesystem::exists(libPath))
			return;

		std::vector<FilePath> filenames;
		for (const auto& file : std::filesystem::directory_iterator(libPath))
		{
			// look only for json files and ignore any dot files present
			std::string extension = IO::toString(file.path().extension());
			std::string filename = IO::toString(file.path().filename());
			if (extension == ".json" && !IO::startsWith(filename, "."))
				filenames.push_back(file.path());
		}

		std::mutex m2;
		presets.reserve(filenames.size());

		std::vector<Result> warnings;
		std::vector<Result> errors;

		auto readPreset = [this, &warnings, &errors, &m2](const auto& filepath)
		{
			int id = nextPresetID++;

			NotesPreset preset(id, "");
			Result result = preset.read(filepath);
			{
				std::lock_guard<std::mutex> lock{ m2 };

				switch (result.getStatus())
				{
				case ResultStatus::Success:
					presets.emplace(id, std::move(preset));
					break;
				case ResultStatus::Warning:
					warnings.push_back(result);
					break;
				case ResultStatus::Error:
					errors.push_back(result);
					break;
				}
			}
		};
		std::for_each(std::execution::par, filenames.begin(), filenames.end(), readPreset);

		if (errors.size())
		{
			std::string message;
			for (auto& error : errors)
				message += "- " + error.getMessage() + "\n";

			IO::messageBox(APP_NAME, message, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
		}

		if (warnings.size())
		{
			std::string message;
			for (auto& warning : warnings)
				message += "- " + warning.getMessage() + "\n";

			IO::messageBox(APP_NAME, message, IO::MessageBoxButtons::Ok,
			               IO::MessageBoxIcon::Warning);
		}
	}

	void PresetManager::savePresets(const FilePath& libPath)
	{
		namespace fs = std::filesystem;

		if (!IO::File::exists(libPath))
			fs::create_directory(libPath);

		for (const std::string& filename : deletePresets)
		{
			auto path = libPath / IO::stringToPath(filename + ".json");
			if (IO::File::exists(path))
				fs::remove(path);
		}

		auto writePreset = [this, &libPath](int id)
		{
			auto it = presets.find(id);
			if (it == presets.end())
				return;
			NotesPreset& preset = presets.at(id);

			auto filepath = IO::toString(fixFilename(preset.getName()));
			preset.write(IO::toString(libPath / filepath.append(".json")));
		};
		std::for_each(std::execution::par, createPresets.begin(), createPresets.end(), writePreset);
	}

	void PresetManager::createPreset(const ScoreContext& context, const std::string& name,
	                                 const std::string& desc)
	{
		if (context.selection.empty() || name.empty())
			return;

		int ID = nextPresetID++;
		createPresets.push_back(ID);
		NotesPreset& preset = presets.emplace(ID, NotesPreset(ID, name)).first->second;
		preset.name = name;
		preset.description = desc;

		tick_t baseTick = context.getMinTickFromSelection();
		selected_score_to_json(preset.data, context.score, context.selection, baseTick,
		                       context.selectedLayer);
	}

	void PresetManager::removePreset(int id)
	{
		if (presets.find(id) == presets.end())
			return;

		const auto& preset = presets.at(id);
		if (preset.getFilename().size())
			deletePresets.push_back(preset.getFilename());

		presets.erase(id);
	}

	std::string PresetManager::fixFilename(const std::string& name)
	{
		std::string result = name;
		constexpr std::array<char, 9> invalidFilenameChars{ '\\', '/', '\"', '|', '<',
			                                                '>',  '?', '*',  ':' };
		for (char c : invalidFilenameChars)
		{
			std::replace(result.begin(), result.end(), c, '_');
		}

		return result;
	}

	void PresetManager::applyPreset(int presetId, PasteData& pasteData)
	{
		auto it = presets.find(presetId);
		if (it == presets.end())
			return;
		pasteData.load(it->second.data);
	}
}

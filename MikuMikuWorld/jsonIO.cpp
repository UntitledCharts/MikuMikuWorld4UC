#include "JsonIO.h"
#include "Math.h"
#include "ApplicationConfiguration.h"
#include "InputBinding.h"
#include "Score.h"
#include "Application.h"

using namespace nlohmann;
using namespace jsonIO;

namespace std::chrono
{
	void to_json(json& j, const system_clock::time_point& tp)
	{
		j = duration_cast<milliseconds>(tp.time_since_epoch()).count();
	}

	void from_json(const json& j, system_clock::time_point& tp)
	{
		milliseconds::rep t = j;
		tp = system_clock::time_point(milliseconds(t));
	}
}

namespace MikuMikuWorld
{
	void to_json(json& j_vec, const Vector2& vec)
	{
		j_vec["x"] = vec.x;
		j_vec["y"] = vec.y;
	}

	void from_json(const json& j_vec, Vector2& vec)
	{
		j_vec.at("x").get_to(vec.x);
		j_vec.at("y").get_to(vec.x);
	}

	void to_json(nlohmann::json& j_color, const Color& color)
	{
		j_color["r"] = color.r;
		j_color["g"] = color.g;
		j_color["b"] = color.b;
		j_color["a"] = color.a;
	}

	void from_json(const nlohmann::json& j_color, Color& color)
	{
		j_color.at("r").get_to(color.r);
		j_color.at("g").get_to(color.g);
		j_color.at("b").get_to(color.b);
		j_color.at("a").get_to(color.a);
	}

	void to_json(nlohmann::json& j_input, const InputConfiguration& input)
	{
		for (const auto& pBinding : allInputBindings)
		{
			auto& binding = (input.*pBinding);
			auto& j_binding = j_input[binding.name.data()] = json::array();
			for (size_t i = 0; i < binding.count; ++i)
				if (binding.bindings[i].keyCode != ImGuiKey_None)
					j_binding.push_back(ToSerializedString(binding.bindings[i]));
		}
	}

	void from_json(const nlohmann::json& j, InputConfiguration& input)
	{
		if (!j.is_object())
			return;
		for (auto& [key, value] : j.items())
		{
			if (!value.is_array() || value.empty())
				continue;
			for (const auto& pBinding : allInputBindings)
			{
				auto& multBinding = (input.*pBinding);
				if (multBinding.name == key)
				{
					size_t maxKeys = std::min(value.size(), multBinding.bindings.size());
					for (size_t i = 0; i < maxKeys; i++)
						multBinding.bindings[i] = FromSerializedString(value[i]);
					break;
				}
			}
		}
	}

	void to_json(json& j_cfg, const ApplicationConfiguration& cfg)
	{
		j_cfg["version"] = cfg.version;
		j_cfg["language"] = cfg.language;
		j_cfg["debug"] = cfg.debugEnabled;
		j_cfg["recent_files"] = cfg.recentFiles;

		{
			auto& j_file = j_cfg["file"];
			j_file["minify_output"] = cfg.minifyOutput;
			j_file["export_format"] = cfg.defaultExportFormat;
		}
		{
			auto& j_window = j_cfg["window"];
			j_window["position"] = cfg.windowPos;
			j_window["size"] = cfg.windowSize;
			j_window["maximized"] = cfg.maximized;
			j_window["vsync"] = cfg.vsync;
			j_window["show_fps"] = cfg.showFPS;
		}
		{
			auto& j_timeline = j_cfg["timeline"];
			j_timeline["lane_width"] = cfg.timelineWidth;
			j_timeline["notes_height"] = cfg.notesHeight;
			j_timeline["notes_skin"] = cfg.notesSkin;
			j_timeline["match_notes_size_to_timeline"] = cfg.matchNotesSizeToTimeline;
			j_timeline["match_timeline_size_to_window"] = cfg.matchTimelineSizeToWindow;
			j_timeline["division"] = cfg.division;
			j_timeline["divisionType"] = cfg.divisionType;
			j_timeline["zoom"] = cfg.zoom;
			j_timeline["zoom_sensitivity"] = cfg.zoomSensitivity;
			j_timeline["lane_opacity"] = cfg.laneOpacity;
			j_timeline["background_brightness"] = cfg.backgroundBrightness;
			j_timeline["draw_background"] = cfg.drawBackground;
			j_timeline["background_image"] = cfg.backgroundImage;
			j_timeline["smooth_scrolling_enable"] = cfg.useSmoothScrolling;
			j_timeline["smooth_scrolling_time"] = cfg.smoothScrollingTime;
			j_timeline["scroll_speed_normal"] = cfg.scrollSpeedNormal;
			j_timeline["scroll_speed_fast"] = cfg.scrollSpeedShift;
			j_timeline["draw_waveform"] = cfg.drawWaveform;
			j_timeline["return_to_last_tick_on_pause"] = cfg.returnToLastSelectedTickOnPause;
			j_timeline["cursor_position_threshold"] = cfg.cursorPositionThreshold;
			j_timeline["show_tick_in_properties"] = cfg.showTickInProperties;
			j_timeline["hide_step_outlines_in_playback"] = cfg.hideStepOutlinesInPlayback;
			j_timeline["stop_at_end"] = cfg.stopPlaybackAtMusicEnd;
		}
		{
			auto& j_theme = j_cfg["theme"];
			j_theme["accent_color"] = cfg.accentColor;
			j_theme["user_color"] = cfg.userColor;
			j_theme["base_theme"] = static_cast<int>(cfg.baseTheme);
		}
		{
			auto& j_save = j_cfg["save"];
			j_save["auto_save_enabled"] = cfg.autoSaveEnabled;
			j_save["auto_save_interval"] = cfg.autoSaveInterval;
			j_save["auto_save_max_count"] = cfg.autoSaveMaxCount;
		}
		{
			auto& j_audio = j_cfg["audio"];
			j_audio["se_profile_path"] = cfg.seProfilePath;
			j_audio["master_volume"] = cfg.masterVolume;
			j_audio["bgm_volume"] = cfg.bgmVolume;
			j_audio["se_volume"] = cfg.seVolume;
		}
		{
			auto& j_update = j_cfg["update"];
			j_update["lastUpdate"] = cfg.lastUpdateCheck;
			j_update["lastVersion"] = cfg.latestFetchAppVersion;
		}
		{
			auto& j_input = j_cfg["config"];
			j_input["bindings"] = cfg.input;
		}
	}

	void from_json(const json& j_cfg, ApplicationConfiguration& cfg)
	{
		optional_get_to(j_cfg, "version", cfg.version);
		optional_get_to(j_cfg, "language", cfg.language);
		optional_get_to(j_cfg, "debug", cfg.debugEnabled);
		optional_get_to(j_cfg, "recent_files", cfg.recentFiles);
		if (cfg.recentFiles.size() > maxRecentFilesEntries)
			cfg.recentFiles.resize(maxRecentFilesEntries);
		if (keyExists(j_cfg, "file"))
		{
			auto& j_file = j_cfg["file"];
			optional_get_to(j_file, "minify_output", cfg.minifyOutput);
			optional_get_to(j_file, "export_format", cfg.defaultExportFormat);
		}
		if (keyExists(j_cfg, "window"))
		{
			const json& j_window = j_cfg["window"];
			optional_get_to(j_window, "maximized", cfg.maximized);
			optional_get_to(j_window, "vsync", cfg.vsync);
			optional_get_to(j_window, "show_fps", cfg.showFPS);
			optional_get_to(j_window, "position", cfg.windowPos);
			optional_get_to(j_window, "size", cfg.windowSize);
		}
		if (keyExists(j_cfg, "timeline"))
		{
			const json& j_timeline = j_cfg["timeline"];
			optional_get_to(j_timeline, "lane_width", cfg.timelineWidth);
			optional_get_to(j_timeline, "notes_height", cfg.notesHeight);
			optional_get_to(j_timeline, "notes_skin", cfg.notesSkin);
			optional_get_to(j_timeline, "match_notes_size_to_timeline",
			                cfg.matchNotesSizeToTimeline);
			optional_get_to(j_timeline, "match_timeline_size_to_window",
			                cfg.matchTimelineSizeToWindow);
			if (j_timeline.contains("divisionType"))
				j_timeline["divisionType"].get_to(cfg.divisionType);
			else
				cfg.divisionType = static_cast<int>(TimeDivisionType::Measure);
			optional_get_to(j_timeline, "division", cfg.division);
			optional_get_to(j_timeline, "zoom", cfg.zoom);
			optional_get_to(j_timeline, "zoom_sensitivity", cfg.zoomSensitivity);
			optional_get_to(j_timeline, "lane_opacity", cfg.laneOpacity);
			optional_get_to(j_timeline, "background_brightness", cfg.backgroundBrightness);
			optional_get_to(j_timeline, "draw_background", cfg.drawBackground);
			optional_get_to(j_timeline, "background_image", cfg.backgroundImage);
			optional_get_to(j_timeline, "smooth_scrolling_enable", cfg.useSmoothScrolling);
			optional_get_to(j_timeline, "smooth_scrolling_time", cfg.smoothScrollingTime);
			optional_get_to(j_timeline, "scroll_speed_normal", cfg.scrollSpeedNormal);
			optional_get_to(j_timeline, "scroll_speed_fast", cfg.scrollSpeedShift);
			optional_get_to(j_timeline, "draw_waveform", cfg.drawWaveform);
			optional_get_to(j_timeline, "return_to_last_tick_on_pause",
			                cfg.returnToLastSelectedTickOnPause);
			optional_get_to(j_timeline, "cursor_position_threshold", cfg.cursorPositionThreshold);
			optional_get_to(j_timeline, "show_tick_in_properties", cfg.showTickInProperties);
			optional_get_to(j_timeline, "hide_step_outlines_in_playback",
			                cfg.hideStepOutlinesInPlayback);
			optional_get_to(j_timeline, "stop_at_end", cfg.stopPlaybackAtMusicEnd);
		}

		if (keyExists(j_cfg, "theme"))
		{
			const json& j_theme = j_cfg["theme"];
			optional_get_to(j_theme, "accent_color", cfg.accentColor);
			optional_get_to(j_theme, "user_color", cfg.userColor);

			int baseTheme = static_cast<int>(cfg.baseTheme);
			optional_get_to(j_theme, "base_theme", baseTheme);
			cfg.baseTheme = static_cast<BaseTheme>(baseTheme);
		}

		if (keyExists(j_cfg, "save"))
		{
			const json& j_save = j_cfg["save"];
			optional_get_to(j_save, "auto_save_enabled", cfg.autoSaveEnabled);
			optional_get_to(j_save, "auto_save_interval", cfg.autoSaveInterval);
			optional_get_to(j_save, "auto_save_max_count", cfg.autoSaveMaxCount);
		}

		if (keyExists(j_cfg, "audio"))
		{
			const json& j_audio = j_cfg["audio"];
			if (keyExists(j_audio, "se_profile_path", detail::value_t::string))
				cfg.seProfilePath = tryGetValue<std::string>(j_audio, "se_profile_path");
			else
				cfg.seProfilePath =
				    IO::formatString("%02d", tryGetValue(j_audio, "se_profile", 0) + 1);
			optional_get_to(j_audio, "master_volume", cfg.masterVolume);
			optional_get_to(j_audio, "bgm_volume", cfg.bgmVolume);
			optional_get_to(j_audio, "se_volume", cfg.seVolume);
		}

		if (keyExists(j_cfg, "update"))
		{
			const json& j_update = j_cfg["update"];
			optional_get_to(j_update, "lastUpdate", cfg.lastUpdateCheck);
			optional_get_to(j_update, "lastVersion", cfg.latestFetchAppVersion);
		}

		if (keyExists(j_cfg, "config"))
		{
			const json& j_input = j_cfg["config"];
			optional_get_to(j_input, "bindings", cfg.input);
		}
	}

	static_assert(std::size(flickTypes) == size_t(FlickType::FlickTypeCount));
	static void to_json(json& j, const FlickType& flick)
	{
		j = arrayGetItemSafe(flickTypes, flick);
	}
	static void from_json(const json& j, FlickType& flick)
	{
		std::string flickString = j.get<std::string>();
		std::transform(flickString.begin(), flickString.end(), flickString.begin(), ::tolower);
		// Maintain compatibility with old flick type names
		if (flickString == "up")
		{
			flick = FlickType::Default;
			return;
		}
		flick = arrayFindOrDefault(flickTypes, flickString, FlickType::None);
	}

	constexpr static const char* easeNames[]{ "linear",      "ease_in",     "ease_out",
		                                      "ease_in_out", "ease_out_in", "ease_none" };
	static_assert(std::size(easeNames) == size_t(EaseType::EaseTypeCount));
	static void to_json(json& j, const EaseType& ease) { j = arrayGetItemSafe(easeNames, ease); }
	static void from_json(const json& j, EaseType& ease)
	{
		std::string easeString = j.get<std::string>();
		std::transform(easeString.begin(), easeString.end(), easeString.begin(), ::tolower);
		// Maintain compatibility with old ease type names
		if (easeString == "in")
		{
			ease = EaseType::EaseIn;
			return;
		}
		if (easeString == "out")
		{
			ease = EaseType::EaseOut;
			return;
		}
		ease = arrayFindOrDefault(easeNames, easeString, EaseType::Linear);
	}

	constexpr static const char* sfxNames[]{ "default",    "none",      "tap",      "flick",
		                                     "trace",      "tick",      "crit_tap", "crit_flick",
		                                     "crit_trace", "crit_tick", "damage" };
	static_assert(std::size(sfxNames) == size_t(SoundEffectType ::SoundEffectTypeCount));
	static void to_json(json& j, const SoundEffectType& sfxType)
	{
		j = arrayGetItemSafe(sfxNames, sfxType);
	}
	static void from_json(const json& j, SoundEffectType& sfxType)
	{
		std::string sfxString = j.get<std::string>();
		std::transform(sfxString.begin(), sfxString.end(), sfxString.begin(), ::tolower);
		sfxType = arrayFindOrDefault(sfxNames, sfxString, SoundEffectType::Default);
	}

	static_assert(std::size(guideColors) == size_t(GuideColor::GuideColorCount));
	static void to_json(json& j, const GuideColor& color)
	{
		j = arrayGetItemSafe(guideColors, color);
	}
	static void from_json(const json& j, GuideColor& color)
	{
		std::string colorString = j.get<std::string>();
		std::transform(colorString.begin(), colorString.end(), colorString.begin(), ::tolower);
		color = arrayFindOrDefault(guideColors, colorString, GuideColor::Green);
	}

	static_assert(std::size(fadeTypes) == size_t(FadeType::FadeTypeCount));
	static void to_json(json& j, const FadeType& fadeType)
	{
		j = arrayGetItemSafe(fadeTypes, fadeType);
	}
	static void from_json(const json& j, FadeType& fadeType)
	{
		std::string fadeString = j.get<std::string>();
		std::transform(fadeString.begin(), fadeString.end(), fadeString.begin(), ::tolower);
		fadeType = arrayFindOrDefault(fadeTypes, fadeString, FadeType::Out);
	}

	constexpr const char* holdStepLayerNames[] = { "top", "bottom" };
	static_assert(std::size(holdStepLayerNames) == size_t(HoldStepLayer::LayerCount));
	static void to_json(json& j, const HoldStepLayer& layer)
	{
		j = arrayGetItemSafe(holdStepLayerNames, layer);
	}
	static void from_json(const json& j, HoldStepLayer& layer)
	{
		std::string layerString = j.get<std::string>();
		std::transform(layerString.begin(), layerString.end(), layerString.begin(), ::tolower);
		layer = arrayFindOrDefault(holdStepLayerNames, layerString, HoldStepLayer::Top);
	}

	constexpr const char* hiSpeedEaseNames[] = { "ease_none", "linear" };
	static_assert(std::size(hiSpeedEaseNames) == size_t(HiSpeedEaseType::EaseTypeCount));
	static void to_json(json& j, const HiSpeedEaseType& ease)
	{
		j = arrayGetItemSafe(hiSpeedEaseNames, ease);
	}
	static void from_json(const json& j, HiSpeedEaseType& ease)
	{
		switch (j.type())
		{
		case json::value_t::number_float:
			// How does this even happened?
			ease = static_cast<HiSpeedEaseType>(j.get<float>());
			break;
		case json::value_t::number_integer:
			ease = static_cast<HiSpeedEaseType>(j.get<int>());
			break;
		case json::value_t::number_unsigned:
			ease = static_cast<HiSpeedEaseType>(j.get<unsigned>());
			break;
		case json::value_t::string:
		{
			std::string easeString = j.get<std::string>();
			std::transform(easeString.begin(), easeString.end(), easeString.begin(), ::tolower);
			ease = arrayFindOrDefault(hiSpeedEaseNames, easeString, HiSpeedEaseType::None);
			break;
		}
		default:
			throw std::runtime_error("Invalid data type for HiSpeedEaseType");
		}
	}

	static void base_note_to_json(json& data, const Note& note, tick_t offsetTick)
	{
		data["tick"] = note.tick + offsetTick;
		data["lane"] = note.lane;
		data["width"] = note.width;
		if (note.canSoundEffect())
			data["soundEffect"] = note.soundEffect;
	}
	static void base_note_from_json(const json& data, Note& note)
	{
		note.tick = tryGetValue<int>(data, "tick", 0);
		note.lane = tryGetValue<float>(data, "lane", 0.f);
		note.width = tryGetValue<float>(data, "width", 3.f);
		note.soundEffect =
		    tryGetValue<SoundEffectType>(data, "soundEffect", SoundEffectType::Default);
	}

	static void dummy_note_to_json(json& data, const Note& note)
	{
		if (note.isHidden())
		{
			data["type"] = "hidden";
			data["dummy"] = false;
		}
		else
		{
			data["type"] = hasFlag(note.flag, NoteFlag::Attached) ? "skip" : "normal";
			data["dummy"] = note.isDummy();
		}
	}
	static void dummy_note_from_json(const json& data, Note& note)
	{
		const std::string* type = tryGetValue<const std::string*>(data, "type");
		if (type && *type == "hidden")
		{
			note.flag = setFlag(note.flag, NoteFlag::Hidden);
			note.flag = setFlag(note.flag, NoteFlag::Dummy, false);
		}
		else
		{
			note.flag = setFlag(note.flag, NoteFlag::Hidden, false);
			note.flag = setFlag(note.flag, NoteFlag::Attached | NoteFlag::NonAttached,
			                    type && *type == "skip");
			note.flag = setFlag(note.flag, NoteFlag::Dummy, tryGetValue(data, "dummy", false));
		}
	}

	static void tap_note_to_json(json& data, const Note& note, tick_t offsetTick)
	{
		base_note_to_json(data, note, offsetTick);
		data["critical"] = hasFlag(note.flag, NoteFlag::Critical);
		data["friction"] = hasFlag(note.flag, NoteFlag::Trace);
		data["flick"] = note.flick;
	}
	static void tap_note_from_json(const json& data, Note& note)
	{
		note.type = NoteType::Tap;
		base_note_from_json(data, note);
		note.flag = setFlag(note.flag, NoteFlag::Critical, tryGetValue(data, "critical", false));
		note.flag = setFlag(note.flag, NoteFlag::Trace, tryGetValue(data, "friction", false));
		note.flick = tryGetValue(data, "flick", FlickType::None);
	}

	static void tick_note_to_json(json& data, const Note& note, tick_t offsetTick)
	{
		base_note_to_json(data, note, offsetTick);
		data["critical"] = hasFlag(note.flag, NoteFlag::Critical);
	}
	static void tick_note_from_json(const json& data, Note& note)
	{
		note.type = NoteType::Tick;
		base_note_from_json(data, note);
		note.flag = setFlag(note.flag, NoteFlag::Critical, tryGetValue(data, "critical", false));
	}

	static void step_note_to_json(json& data, const Note& note, const HoldNoteStep& step,
	                              tick_t offsetTick)
	{
		switch (note.type)
		{
		case NoteType::Tap:
			tap_note_to_json(data, note, offsetTick);
			data["extype"] = "tap";
			break;
		case NoteType::Tick:
			tick_note_to_json(data, note, offsetTick);
			data["extype"] = "tick";
			break;
		case NoteType::Damage:
			base_note_to_json(data, note, offsetTick);
			data["extype"] = "damage";
			break;
		default:
			throw std::runtime_error("Unsupported NoteType!");
		}
		if (note.isHidden())
		{
			data["type"] = "hidden";
			data["dummy"] = false;
		}
		else
		{
			data["type"] = note.isAttached() ? "skip" : "normal";
			data["dummy"] = note.isDummy();
		}
		data["ease"] = note.ease;
		data["alpha"] = note.guideAlpha;
	}
	static void step_note_from_json(const json& data, Note& note, const HoldNoteStep& step)
	{
		const std::string* extype = tryGetValue<const std::string*>(data, "extype");
		if (extype && *extype == "tap")
			tap_note_from_json(data, note);
		else if (extype && *extype == "damage")
		{
			note.type = NoteType::Damage;
			base_note_from_json(data, note);
		}
		else
			// default to tick note for step note
			tick_note_from_json(data, note);
		const std::string* type = tryGetValue<const std::string*>(data, "type");
		if (type && (*type == "hidden" || *type == "invisible"))
		{
			note.flag = setFlag(note.flag, NoteFlag::Hidden);
			note.flag = setFlag(note.flag, NoteFlag::Dummy, false);
		}
		else
		{
			if (type && (*type == "skip" || *type == "ignored"))
				note.flag = setFlag(note.flag, NoteFlag::Attached);
			note.flag = setFlag(note.flag, NoteFlag::Dummy, tryGetValue(data, "dummy", false));
		}
		note.ease = tryGetValue(data, "ease", EaseType::Linear);
		optional_get_to(data, "alpha", note.guideAlpha);
		if (!keyExists(data, "critical", json::value_t::boolean))
			note.flag = setFlag(note.flag, NoteFlag::Critical, step.isCrit());
	}

	static void terminal_step_note_to_json(json& data, const Note& note, const HoldNoteStep& step,
	                                       tick_t offsetTick)
	{
		switch (note.type)
		{
		case NoteType::Tap:
			tap_note_to_json(data, note, offsetTick);
			data["extype"] = "tap";
			break;
		case NoteType::Tick:
			tick_note_to_json(data, note, offsetTick);
			data["extype"] = "tick";
			break;
		case NoteType::Damage:
			tap_note_to_json(data, note, offsetTick);
			data["extype"] = "damage";
			break;
		default:
			throw std::runtime_error("Unsupported NoteType!");
		}
		if (step.isGuide())
		{
			data["type"] = "guide";
			data["dummy"] = note.isDummy();
			data["ntype"] = note.isHidden()                          ? "hidden"
			                : hasFlag(note.flag, NoteFlag::Attached) ? "skip"
			                                                         : "normal";
		}
		else if (note.isHidden())
		{
			data["type"] = "hidden";
			data["dummy"] = false;
		}
		else
		{
			data["type"] = hasFlag(note.flag, NoteFlag::Attached) ? "skip" : "normal";
			data["dummy"] = note.isDummy();
		}
		data["ease"] = note.ease;
		data["alpha"] = note.guideAlpha;
	}
	static void terminal_step_note_from_json(const json& data, Note& note, const json& holdData,
	                                         HoldNoteStep& step)
	{
		const std::string* extype = tryGetValue<const std::string*>(data, "extype");
		if (extype && *extype == "tick")
			tick_note_from_json(data, note);
		else if (extype && *extype == "damage")
		{
			tap_note_from_json(data, note);
			note.type = NoteType::Damage;
		}
		else
			tap_note_from_json(data, note);
		const std::string* type = tryGetValue<const std::string*>(data, "type");
		if (type && *type == "guide")
		{
			step.flag = setFlag(step.flag, HoldNoteFlag::Guide);
			const std::string* ntype = tryGetValue<const std::string*>(data, "ntype");
			note.flag = setFlag(note.flag, NoteFlag::Attached, ntype && *ntype == "skip");
			note.flag = setFlag(note.flag, NoteFlag::Hidden, !ntype || *ntype == "hidden");
			note.flag = setFlag(note.flag, NoteFlag::Dummy, tryGetValue(data, "dummy", false));
		}
		else if (type && *type == "hidden")
		{
			step.flag = setFlag(step.flag, HoldNoteFlag::Guide, false);
			note.flag = setFlag(note.flag, NoteFlag::Hidden);
			note.flag = setFlag(note.flag, NoteFlag::Dummy, false);
		}
		else
		{
			step.flag = setFlag(step.flag, HoldNoteFlag::Guide, false);
			note.flag = setFlag(note.flag, NoteFlag::Hidden, false);
			if (type && *type == "skip")
				note.flag = setFlag(note.flag, NoteFlag::Attached);
			note.flag = setFlag(note.flag, NoteFlag::Dummy, tryGetValue(data, "dummy", false));
		}
		note.flag = setFlag(note.flag, NoteFlag::LongNote);
		note.ease = tryGetValue(data, "ease", EaseType::Linear);
		optional_get_to(data, "alpha", note.guideAlpha);
	}

	static void hold_note_step_to_json(json& data, const HoldNoteStep& step)
	{
		if (!step.isGuide())
		{
			data["critical"] = step.isCrit();
			data["dummy"] = step.isDummy();
		}
		// else
		{
			// Not making the properties exclusive allow compatibility with CC version
			data["guide"] = step.guideColor;
		}
		data["layer"] = step.layer;
	}
	static void hold_note_step_from_json(const json& data, HoldNoteStep& step,
	                                     const json& startData)
	{
		step.guideColor = tryGetValue(data, "guide", GuideColor::Green);
		step.flag = setFlag(step.flag, HoldNoteFlag::Dummy, tryGetValue(data, "dummy", false));
		bool critical;
		if (keyExists(data, "critical", json::value_t::boolean))
			critical = data["critical"];
		else
			critical = tryGetValue(startData, "critical", false);
		step.flag = setFlag(step.flag, HoldNoteFlag::Critical, critical);
		step.layer = tryGetValue(data, "layer", HoldStepLayer::Top);
	}

	static void hispeed_to_json(json& data, const HiSpeed& hispeed, tick_t offsetTick)
	{
		data["tick"] = hispeed.tick + offsetTick;
		data["speed"] = hispeed.speed;
		data["skip"] = hispeed.skips;
		data["ease"] = hispeed.ease == HiSpeedEaseType::Linear ? 1 : 0; // for backward compat
		data["easeType"] = hispeed.ease;
		data["hideNotes"] = hispeed.hideNotes;
	}
	static void hispeed_from_json(const json& data, HiSpeed& hispeed)
	{
		hispeed.tick = data["tick"];
		hispeed.speed = tryGetValue(data, "speed", 1.f);
		hispeed.skips = tryGetValue(data, "skip", 0.0f);
		const char* easeKey =
		    keyExists(data, "easeType", json::value_t::string) ? "easeType" : "ease";
		hispeed.ease = tryGetValue(data, easeKey, HiSpeedEaseType::None);
		hispeed.hideNotes = tryGetValue(data, "hideNotes", false);
	}

	void selected_score_to_json(json& data, const Score& score, const ScoreSelection& selection,
	                            tick_t baseTick, id_t currentLayer)
	{
		json& notes = data["notes"] = json::array();
		json& damages = data["damages"] = json::array();
		json& ticks = data["ticks"] = json::array();
		json& holds = data["holds"] = json::array();
		json& hiSpeedChanges = data["hiSpeedChanges"] = json::array();
		data["origin"] = APP_NAME;
		data["version"] = Application::getInstance().getAppVersion();

		std::vector<const Note*> selectedNotes;
		std::unordered_set<id_t> selectedHolds;
		std::unordered_map<id_t, std::vector<const Note*>> selectedSteps;
		for (auto&& noteID : selection.notes)
		{
			const Note& note = score.notes.at(noteID);
			selectedNotes.push_back(&note);
			if (!note.isHold())
				continue;
			selectedHolds.emplace(note.holdID);
			selectedSteps[note.holdID].push_back(&note);
		}
		// Remove any holds that doesn't have at least 2 steps
		for (auto it = selectedSteps.begin(); it != selectedSteps.end();)
		{
			if (it->second.size() < 2)
			{
				selectedHolds.erase(it->first);
				it = selectedSteps.erase(it);
			}
			else
				++it;
		}

		for (auto&& pnote : selectedNotes)
		{
			if (pnote->isHold() && selectedHolds.count(pnote->holdID))
				continue;
			json* pdata;
			switch (pnote->type)
			{
			case NoteType::Tap:
				pdata = &notes.emplace_back();
				tap_note_to_json(*pdata, *pnote, -baseTick);
				break;
			case NoteType::Tick:
				pdata = &ticks.emplace_back();
				tick_note_to_json(*pdata, *pnote, -baseTick);
				break;
			case NoteType::Damage:
				pdata = &damages.emplace_back();
				tap_note_to_json(*pdata, *pnote, -baseTick);
				break;
			default:
				throw std::runtime_error("Unsupported NoteType!");
			}
			dummy_note_to_json(*pdata, *pnote);
		}
		for (auto&& [id, steps] : selectedSteps)
		{
			std::stable_sort(steps.begin(), steps.end(), HoldNote::StepComparer());
			auto stepIt = steps.begin(), endIt = steps.end();

			const HoldNote& hold = score.holdNotes.at(id);
			auto nextHoldStepIt =
			    std::upper_bound(hold.separators.begin(), hold.separators.end(), *steps.front(),
			                     HoldNote::HoldStepComparer(score.notes));
			const HoldNoteStep* holdStep = &*std::prev(nextHoldStepIt);

			json* holdData = &holds.emplace_back();
			hold_note_step_to_json(*holdData, *holdStep);
			(*holdData)["fade"] = hold.fadeType;

			json* holdStart = &(*holdData)["start"];
			const Note* start = *stepIt;
			terminal_step_note_to_json(*holdStart, *start, *holdStep, -baseTick);

			++stepIt, --endIt;
			json* stepsArray = &((*holdData)["steps"] = json::array());
			for (; stepIt != endIt; ++stepIt)
			{
				const Note& step = **stepIt;
				if (nextHoldStepIt != hold.separators.end() && nextHoldStepIt->ID == step.ID)
				{
					++nextHoldStepIt;
					break;
				}
				step_note_to_json(stepsArray->emplace_back(), step, *holdStep, -baseTick);
			}

			while (stepIt != endIt)
			{
				holdData = &(*holdData)["next"];
				holdStart = &(*holdData)["start"];
				start = *stepIt;

				holdStep = &*std::prev(nextHoldStepIt);
				hold_note_step_to_json(*holdData, *holdStep);
				terminal_step_note_to_json(*holdStart, *start, *holdStep, -baseTick);

				++stepIt;
				stepsArray = &((*holdData)["steps"] = json::array());
				for (; stepIt != endIt; ++stepIt)
				{
					const Note& step = **stepIt;
					if (nextHoldStepIt != hold.separators.end() && nextHoldStepIt->ID == step.ID)
					{
						++nextHoldStepIt;
						break;
					}
					step_note_to_json(stepsArray->emplace_back(), step, *holdStep, -baseTick);
				}
			}

			json& holdEnd = (*holdData)["end"];
			const Note& end = **stepIt;
			terminal_step_note_to_json(holdEnd, end, *holdStep, -baseTick);
		}
		for (auto&& [layer, tick] : selection.hispeeds)
		{
			// If 2 hispeeds at the same tick are selected,
			// always prioritise the one in the current layer
			if (layer != currentLayer && selection.hispeeds.count({ currentLayer, tick }))
				continue;
			hispeed_to_json(hiSpeedChanges.emplace_back(),
			                score.layers[layer].hiSpeedChanges.at(tick), -baseTick);
		}
	}

	bool is_paste_data_empty(const nlohmann::json& data)
	{
		return !arrayHasData(data, "notes") && !arrayHasData(data, "damages") &&
		       !arrayHasData(data, "ticks") && !arrayHasData(data, "holds") &&
		       !arrayHasData(data, "hiSpeedChanges");
	}

	void paste_data_from_json(const json& data, PasteData& pasteData)
	{
		int nextNoteID = 0, nextHoldID = 0;
		pasteData.notes.clear();
		pasteData.holdNotes.clear();
		pasteData.hiSpeedChanges.clear();

		if (arrayHasData(data, "notes"))
		{
			for (const auto& entry : data["notes"])
			{
				Note& note = pasteData.notes[nextNoteID];
				note.ID = nextNoteID++;
				tap_note_from_json(entry, note);
				dummy_note_from_json(entry, note);
			}
		}

		if (arrayHasData(data, "damages"))
		{
			for (const auto& entry : data["damages"])
			{
				Note& note = pasteData.notes[nextNoteID];
				note.ID = nextNoteID++;
				note.type = NoteType::Damage;
				base_note_from_json(entry, note);
				dummy_note_from_json(entry, note);
			}
		}

		if (arrayHasData(data, "ticks"))
		{
			for (const auto& entry : data["ticks"])
			{
				Note& note = pasteData.notes[nextNoteID];
				note.ID = nextNoteID++;
				tick_note_from_json(entry, note);
				dummy_note_from_json(entry, note);
			}
		}

		if (arrayHasData(data, "holds"))
		{
			for (const auto& entry : data["holds"])
			{
				if (!keyExists(entry, "start", json::value_t::object))
					continue;

				const json* holdStepData = &entry;
				const json* holdStart = &entry["start"];
				HoldNote& hold = pasteData.holdNotes[nextHoldID];
				HoldNoteStep* holdStep = &hold.separators.emplace_back();
				hold.ID = nextHoldID++;
				hold_note_step_from_json(*holdStepData, *holdStep, *holdStart);
				hold.fadeType = tryGetValue(entry, "fade", FadeType::Out);

				Note* start = &pasteData.notes[nextNoteID];
				start->ID = nextNoteID++;
				start->holdID = hold.ID;
				terminal_step_note_from_json(*holdStart, *start, *holdStepData, *holdStep);
				holdStep->ID = start->ID;
				hold.steps.push_back(start->ID);

				if (arrayHasData(*holdStepData, "steps"))
				{
					const json& stepsArray = holdStepData->at("steps");
					hold.steps.reserve(stepsArray.size() + 2);
					for (const auto& step : stepsArray)
					{
						Note& mid = pasteData.notes[nextNoteID];
						mid.ID = nextNoteID++;
						mid.holdID = hold.ID;
						step_note_from_json(step, mid, *holdStep);
						hold.steps.push_back(mid.ID);
					}
				}

				while (keyExists(*holdStepData, "next", json::value_t::object))
				{
					holdStepData = &(*holdStepData)["next"];
					holdStart = &(*holdStepData)["start"];
					holdStep = &hold.separators.emplace_back();
					hold_note_step_from_json(*holdStepData, *holdStep, *holdStart);

					start = &pasteData.notes[nextNoteID];
					start->ID = nextNoteID++;
					start->holdID = hold.ID;
					terminal_step_note_from_json(*holdStart, *start, *holdStepData, *holdStep);
					holdStep->ID = start->ID;
					hold.steps.push_back(start->ID);

					if (!arrayHasData(*holdStepData, "steps"))
						continue;
					const json& stepsArray = holdStepData->at("steps");
					hold.steps.reserve(hold.steps.size() + stepsArray.size() + 2);
					for (const auto& step : stepsArray)
					{
						Note& mid = pasteData.notes[nextNoteID];
						mid.ID = nextNoteID++;
						mid.holdID = hold.ID;
						step_note_from_json(step, mid, *holdStep);
						hold.steps.push_back(mid.ID);
					}
				}

				Note& end = pasteData.notes[nextNoteID];
				end.ID = nextNoteID++;
				end.holdID = hold.ID;
				if (keyExists(*holdStepData, "end"))
					terminal_step_note_from_json((*holdStepData)["end"], end, *holdStepData,
					                             *holdStep);

				hold.steps.push_back(end.ID);
				hold.sortSteps(pasteData.notes);
			}
		}

		if (arrayHasData(data, "hiSpeedChanges"))
		{
			for (const auto& entry : data["hiSpeedChanges"])
			{
				if (!keyExists(entry, "tick"))
					continue;
				HiSpeed hispeed;
				hispeed_from_json(entry, hispeed);
				pasteData.hiSpeedChanges[hispeed.tick] = hispeed;
			}
		}

		pasteData.pasting =
		    pasteData.notes.size() || pasteData.holdNotes.size() || pasteData.hiSpeedChanges.size();
	}
}
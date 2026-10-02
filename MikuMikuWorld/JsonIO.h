#pragma once
#include <json.hpp>
#include <unordered_set>
#include "Note.h"
#include "ScoreEvents.h"

namespace jsonIO
{
	static bool keyExists(const nlohmann::json& js, const char* key) { return js.contains(key); }

	static bool keyExists(const nlohmann::json& js, const char* key, nlohmann::json::value_t type)
	{
		auto it = js.find(key);
		return (it != js.end()) && it->type() == type;
	}

	static bool arrayHasData(const nlohmann::json& js, const char* key)
	{
		return jsonIO::keyExists(js, key, nlohmann::json::value_t::array) && js[key].size();
	}

	template <typename T> T tryGetValue(const nlohmann::json& js, const char* key, T def = {})
	{
		return keyExists(js, key) ? js[key].template get<T>() : std::move(def);
	}

	template <typename T, typename TKey>
	static auto optional_get_to(const nlohmann::json& j, TKey&& k, T& v)
	    -> decltype(j.get_to(v), T())
	{
		if (j.contains(k))
			return j[k].get_to(v);
		return v;
	}
}

namespace std::chrono
{
	void to_json(nlohmann::json& j, const system_clock::time_point& tp);
	void from_json(const nlohmann::json& j, system_clock::time_point& tp);
}

namespace MikuMikuWorld
{
	struct Vector2;
	void to_json(nlohmann::json& j, const Vector2& vec);
	void from_json(const nlohmann::json& j, Vector2& vec);

	struct Color;
	void to_json(nlohmann::json& j, const Color& color);
	void from_json(const nlohmann::json& j, Color& color);

	struct InputConfiguration;
	void to_json(nlohmann::json& j, const InputConfiguration& input);
	void from_json(const nlohmann::json& j, InputConfiguration& input);

	struct ApplicationConfiguration;
	void to_json(nlohmann::json& j, const ApplicationConfiguration& cfg);
	void from_json(const nlohmann::json& j, ApplicationConfiguration& cfg);

	struct Score;
	struct ScoreSelection;
	void selected_score_to_json(nlohmann::json& data, const Score& score,
	                            const ScoreSelection& selection, tick_t baseTick,
	                            id_t currentLayer);
	struct PasteData;
	bool is_paste_data_empty(const nlohmann::json& data);
	void paste_data_from_json(const nlohmann::json& data, PasteData& pasteData);
}
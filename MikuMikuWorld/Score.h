#pragma once
#include "Constants.h"
#include "Note.h"
#include "Tempo.h"
#include <cstdint>
#include <map>
#include <string>
#include <set>
#include <vector>

namespace MikuMikuWorld
{
	struct Layer
	{
		std::string name;
		float forceNoteSpeed = 0.0f;
		bool hidden = false;
		HiSpeedCollection hiSpeedChanges;

		Layer(std::string_view name, float forceNoteSpeed = 0.0f)
		    : name(name), forceNoteSpeed(forceNoteSpeed)
		{
		}
		Layer(id_t id, std::string_view name, float forceNoteSpeed = 0.0f)
		    : name(name), forceNoteSpeed(forceNoteSpeed)
		{
			hiSpeedChanges.emplace(0, HiSpeed{ 0, id });
		}
	};
	using LayerCollection = std::vector<Layer>;

	struct Waypoint
	{
		id_t ID;
		tick_t tick;
		std::string name;
	};
	using WaypointCollection = std::map<id_t, Waypoint>;
	using WaypointOrderedCollection = std::multimap<tick_t, Waypoint*>;

	struct ScoreMetadata
	{
		std::string title;
		std::string artist;
		std::string author;
		std::string musicFile;
		std::string jacketFile;
		float musicOffset = 0.0f;

		int laneExtension = 0;
		int baseLifePoint = 1000;
		bool isExtendedScore = true;
	};

	struct Score : public NotesContext
	{
	  public:
		TempoCollection tempoChanges{ {} };           // always have 1 tempo change
		TimeSignatureCollection timeSignatures{ {} }; // always have 1 timesign change
		SkillTriggerCollection skills;
		Fever fever;

		LayerCollection layers{ Layer{ "default", 0 } };
		WaypointCollection waypoints;
	};

	struct ScoreSelection
	{
		NoteRefCollection notes;
		HiSpeedRefCollection hispeeds;

		bool empty() const;
		bool emptyNotes() const;
		bool emptyHispeeds() const;
		bool hasNoteID(id_t noteID) const;
		bool has(const Note& note) const;
		bool has(const HiSpeed& hispeed) const;

		void clearAll();
		void clearNotes();
		void clearHispeed();
		bool operator==(const ScoreSelection& other) const;
	};
}

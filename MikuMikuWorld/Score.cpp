#include "Score.h"
#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "Constants.h"
#include "File.h"
#include "IO.h"
#include <unordered_set>

using namespace IO;

namespace MikuMikuWorld
{
	bool ScoreSelection::empty() const { return notes.empty() && hispeeds.empty(); }
	bool ScoreSelection::emptyNotes() const { return notes.empty(); }
	bool ScoreSelection::emptyHispeeds() const { return hispeeds.empty(); }

	bool ScoreSelection::hasNoteID(id_t noteID) const { return notes.count(noteID); }
	bool ScoreSelection::has(const Note& note) const { return notes.count(note.ID); }
	bool ScoreSelection::has(const HiSpeed& hispeed) const
	{
		return hispeeds.count({ hispeed.layer, hispeed.tick });
	}

	void ScoreSelection::clearAll()
	{
		clearNotes();
		clearHispeed();
	}
	void ScoreSelection::clearNotes() { notes.clear(); }
	void ScoreSelection::clearHispeed() { hispeeds.clear(); }

	bool ScoreSelection::operator==(const ScoreSelection& other) const
	{
		return notes == other.notes && hispeeds == other.hispeeds;
	}
}

#pragma once
#include "HistoryEdit.h"
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace MikuMikuWorld
{
	struct History
	{
		std::string description;
		HistoryEdit edit;
		// Selection to restore on undo (before) / redo (after).
		// When absent, the selection is cleared.
		std::optional<FieldChange<ScoreSelection>> selection;
	};

	class HistoryManager
	{
	  private:
		// Number of entries currently applied. Entries [0, cursor) can be undone,
		// entries [cursor, size) can be redone.
		size_t cursor{ 0 };
		std::vector<History> historyStack;

		using iterator = std::vector<History>::const_reverse_iterator;

	  public:
		void pushHistory(History history);
		void undo(HistoryContext& ctx);
		void redo(HistoryContext& ctx);

		int undoCount() const;
		int redoCount() const;
		bool hasUndo() const;
		bool hasRedo() const;
		void clear();

		// (newest, first undoable, end): [newest, first undoable) are redoable, newest first.
		std::tuple<iterator, iterator, iterator> getHistories() const;
	};
}

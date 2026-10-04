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
		// A null SelectionRef clears the selection.
		SelectionChange selection;
	};

	class HistoryManager
	{
	  private:
		// Number of entries currently applied. Entries [0, cursor) can be undone,
		// entries [cursor, size) can be redone.
		size_t cursor{ 0 };
		std::vector<History> historyStack;

		using const_iterator = std::vector<History>::const_reverse_iterator;

	  public:
		void pushHistory(History history);
		void undo(HistoryContext& ctx);
		void redo(HistoryContext& ctx);

		int undoCount() const;
		int redoCount() const;
		bool hasUndo() const;
		bool hasRedo() const;
		void clear();

		// (current, begin, end)
		std::tuple<const_iterator, const_iterator, const_iterator> getHistories() const;
	};
}

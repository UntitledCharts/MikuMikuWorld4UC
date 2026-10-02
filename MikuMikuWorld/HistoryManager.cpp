#include "HistoryManager.h"

namespace MikuMikuWorld
{
	void HistoryManager::pushHistory(History history)
	{
		// Branching: pushing after an undo discards the redo entries
		if (cursor < historyStack.size())
			historyStack.erase(historyStack.begin() + cursor, historyStack.end());

		historyStack.push_back(std::move(history));
		cursor = historyStack.size();
	}

	void HistoryManager::undo(HistoryContext& ctx)
	{
		if (!hasUndo())
			return;

		const History& entry = historyStack[--cursor];
		undoEdit(entry.edit, ctx);
		if (entry.selection)
			ctx.selection = entry.selection->before;
		else
			ctx.selection.clearAll();
	}

	void HistoryManager::redo(HistoryContext& ctx)
	{
		if (!hasRedo())
			return;

		const History& entry = historyStack[cursor++];
		redoEdit(entry.edit, ctx);
		if (entry.selection)
			ctx.selection = entry.selection->after;
		else
			ctx.selection.clearAll();
	}

	int HistoryManager::undoCount() const { return static_cast<int>(cursor); }

	int HistoryManager::redoCount() const { return static_cast<int>(historyStack.size() - cursor); }

	bool HistoryManager::hasUndo() const { return cursor > 0; }

	bool HistoryManager::hasRedo() const { return cursor < historyStack.size(); }

	void HistoryManager::clear()
	{
		historyStack.clear();
		cursor = 0;
	}

	std::tuple<HistoryManager::iterator, HistoryManager::iterator, HistoryManager::iterator>
	HistoryManager::getHistories() const
	{
		return std::make_tuple(historyStack.crbegin(), historyStack.crend() - cursor,
		                       historyStack.crend());
	}
}

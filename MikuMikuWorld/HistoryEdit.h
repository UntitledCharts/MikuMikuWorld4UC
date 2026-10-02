#pragma once
#include "Score.h"
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

namespace MikuMikuWorld
{
	// Mutable state that history operations are allowed to touch.
	struct HistoryContext
	{
		Score& score;
		ScoreMetadata& metadata;
		ScoreSelection& selection;
	};

	template <typename T> struct FieldChange
	{
		T before;
		T after;
	};

	// ---- Metadata ----------------------------------------------------------------------------
	template <typename T> struct MetadataChange
	{
		T ScoreMetadata::* field;
		FieldChange<T> change;
	};

	using MetadataFieldChange = std::variant<MetadataChange<std::string>, MetadataChange<float>,
											 MetadataChange<int>, MetadataChange<bool>>;

	struct ChangeMetadata
	{
		MetadataFieldChange change;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// Every alternative must provide: void undo(HistoryContext&) const; void redo(HistoryContext&) const;
	using HistoryEdit = std::variant<ChangeMetadata>;

	void undoEdit(const HistoryEdit& edit, HistoryContext& ctx);
	void redoEdit(const HistoryEdit& edit, HistoryContext& ctx);
}

#include "HistoryEdit.h"

namespace MikuMikuWorld
{
	namespace
	{
		template <typename T>
		void collect(std::vector<MetadataFieldChange>& out, T ScoreMetadata::* field,
		             const ScoreMetadata& before, const ScoreMetadata& after)
		{
			if (before.*field != after.*field)
				out.push_back(MetadataChange<T>{ field, { before.*field, after.*field } });
		}

		void apply(ScoreMetadata& metadata, const std::vector<MetadataFieldChange>& fields,
		           bool useAfter)
		{
			for (const auto& field : fields)
			{
				std::visit([&](const auto& c)
				           { metadata.*(c.field) = useAfter ? c.change.after : c.change.before; },
				           field);
			}
		}
	}
	
	void ChangeMetadata::undo(HistoryContext& ctx) const {
		std::visit([&](const auto& c)
				           { ctx.metadata.*(c.field) = c.change.before; },
				           change);

	 }

	void ChangeMetadata::redo(HistoryContext& ctx) const { std::visit([&](const auto& c)
		{ ctx.metadata.*(c.field) = c.change.after; },
		change); }

	void undoEdit(const HistoryEdit& edit, HistoryContext& ctx)
	{
		std::visit([&ctx](const auto& e) { e.undo(ctx); }, edit);
	}

	void redoEdit(const HistoryEdit& edit, HistoryContext& ctx)
	{
		std::visit([&ctx](const auto& e) { e.redo(ctx); }, edit);
	}
}

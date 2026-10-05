#include "HistoryEdit.h"
#include "Utilities.h"
#include <algorithm>

namespace MikuMikuWorld
{
	// Helpers
	namespace
	{
		static bool operator==(const Note& a, const Note& b)
		{
			return isSame(a, b) && a.holdID == b.holdID;
		}

		static bool operator==(const HoldNote& a, const HoldNote& b)
		{
			return a.fadeType == b.fadeType && a.steps == b.steps && a.joints == b.joints &&
			       a.separators.size() == b.separators.size() &&
			       std::equal(a.separators.begin(), a.separators.end(), b.separators.begin(),
			                  [](const HoldNoteStep& x, const HoldNoteStep& y)
			                  { return x.ID == y.ID && isSame(x, y); });
		}

		template <typename Container, typename = void> struct has_reserve : std::false_type
		{
		};

		template <typename T>
		using reserve_return_t =
		    decltype(std::declval<T&>().reserve(std::declval<typename T::size_type>()));

		template <typename Container>
		struct has_reserve<Container, std::void_t<reserve_return_t<Container>>> : std::true_type
		{
		};

		template <typename Container> void moveExtend(Container& base, Container&& other)
		{
			if constexpr (has_reserve<Container>::value)
			{
				base.reserve(base.size() + other.size());
			}
			base.insert(base.end(), std::make_move_iterator(other.begin()),
			            std::make_move_iterator(other.end()));
			other.clear();
		}

		template <typename T> struct is_std_map : std::false_type
		{
		};

		template <typename K, typename V, typename C, typename A>
		struct is_std_map<std::map<K, V, C, A>> : std::true_type
		{
		};

		template <typename M, typename FIt, typename FDf, typename FRDf>
		typename std::enable_if<is_std_map<M>::value>::type
		for_each_relations(const M& map1, const M& map2, FIt onIntersect, FDf onDifference,
		                   FRDf onRevDifference)
		{
			auto it1 = map1.begin();
			auto it2 = map2.begin();
			while (it1 != map1.end() && it2 != map2.end())
			{
				if (it1->first < it2->first)
				{
					onDifference(*it1);
					++it1;
				}
				else if (it2->first < it1->first)
				{
					onRevDifference(*it2);
					++it2;
				}
				else
				{
					onIntersect(*it1, *it2);
					++it1;
					++it2;
				}
			}
			while (it1 != map1.end())
			{
				onDifference(*it1);
				++it1;
			}
			while (it2 != map2.end())
			{
				onRevDifference(*it2);
				++it2;
			}
		}

		template <typename M, typename FIt, typename FDf, typename FRDf>
		typename std::enable_if<is_std_map<M>::value == false>::type
		for_each_relations(const M& map1, const M& map2, FIt onIntersect, FDf onDifference,
		                   FRDf onRevDifference)
		{
			for (auto it1 = map1.begin(); it1 != map1.end(); ++it1)
			{
				auto it2 = map2.find(it1->first);
				if (it2 != map2.end())
					onIntersect(*it1, *it2);
				else
					onDifference(*it1);
			}
			for (auto it2 = map2.begin(); it2 != map2.end(); ++it2)
			{
				if (map1.find(it2->first) == map1.end())
					onRevDifference(*it2);
			}
		}
	}

	void MetadataEdit::undo(HistoryContext& ctx) const
	{
		std::visit([&](const auto& c) { ctx.metadata.*(c.field) = c.change.before; }, change);
	}

	void MetadataEdit::redo(HistoryContext& ctx) const
	{
		std::visit([&](const auto& c) { ctx.metadata.*(c.field) = c.change.after; }, change);
	}

	void NoteTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		if (value)
			score.notes.insert_or_assign(key, *value);
		else
			score.notes.erase(key);
	}

	void HoldTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		if (value)
			score.holdNotes.insert_or_assign(key, *value);
		else
			score.holdNotes.erase(key);
	}

	bool ScoreEdit::empty() const
	{
		return MultiNoteEdit::changes.empty() && MultiHoldEdit::changes.empty() &&
		       MultiHiSpeedEdit::changes.empty();
	}

	void ScoreEdit::undo(HistoryContext& ctx) const
	{
		MultiNoteEdit::undo(ctx);
		MultiHoldEdit::undo(ctx);
		MultiHiSpeedEdit::undo(ctx);
	}

	void ScoreEdit::redo(HistoryContext& ctx) const
	{
		MultiNoteEdit::redo(ctx);
		MultiHoldEdit::redo(ctx);
		MultiHiSpeedEdit::redo(ctx);
	}

	void ScoreEdit::merge(ScoreEdit&& other)
	{
		moveExtend(notes(), std::move(other.notes()));
		moveExtend(holds(), std::move(other.holds()));
		moveExtend(hispeeds(), std::move(other.hispeeds()));
	}

	HistoryEdit toHistoryEdit(ScoreEdit&& edit)
	{
		bool singleEdit = edit.notes().size() + edit.holds().size() + edit.hispeeds().size() == 1;
		if (edit.notes().size() == 1 && singleEdit)
			return SingleNoteEdit{ std::move(edit.notes().front()) };
		if (edit.holds().size() == 1 && singleEdit)
			return SingleHoldEdit{ std::move(edit.holds().front()) };
		if (edit.hispeeds().size() == 1 && singleEdit)
			return SingleHiSpeedEdit{ std::move(edit.hispeeds().front()) };
		return std::move(edit);
	}

	void ScoreCapture::captureNote(const NotesContext& context, id_t noteID)
	{
		if (notes.count(noteID))
			return;
		auto it = context.notes.find(noteID);
		if (it != context.notes.end())
			notes.emplace(noteID, it->second);
	}

	void ScoreCapture::captureHold(const NotesContext& context, id_t holdID, bool steps)
	{
		if (holdNotes.count(holdID))
			return;
		auto it = context.holdNotes.find(holdID);
		if (it == context.holdNotes.end())
			return;
		const HoldNote& hold = it->second;
		holdNotes.emplace(holdID, hold);
		if (!steps)
			return;
		for (id_t step : hold.steps)
			captureNote(context, step);
	}

	void ScoreCapture::captureHiSpeed(const Score& score, const layered_tick_t& key)
	{
		auto&& [layer, tick] = key;
		if (hispeeds.count(key) || !isArrayIndexInBounds(layer, score.layers))
			return;

		const HiSpeedCollection& hiSpeedChanges = score.layers[layer].hiSpeedChanges;
		auto it = hiSpeedChanges.find(tick);
		if (it != hiSpeedChanges.end())
			hispeeds.emplace(key, it->second);
	}

	void ScoreCapture::captureCreated(const Score& score, id_t beforeNoteID, id_t afterNoteID,
	                                  id_t beforeHoldID, id_t afterHoldID)
	{
		for (id_t id = beforeNoteID; id < afterNoteID; ++id)
			captureNote(score, id);
		for (id_t id = beforeHoldID; id < afterHoldID; ++id)
			captureHold(score, id, false);
	}

	void ScoreCapture::merge(const ScoreCapture& other)
	{
		notes.insert(other.notes.begin(), other.notes.end());
		holdNotes.insert(other.holdNotes.begin(), other.holdNotes.end());
		hispeeds.insert(other.hispeeds.begin(), other.hispeeds.end());
	}

	ScoreCapture ScoreCapture::recapture(const Score& score) const
	{
		ScoreCapture capture;
		for (const auto& [id, _] : notes)
		{
			auto it = score.notes.find(id);
			if (it != score.notes.end())
				capture.notes.emplace(id, it->second);
		}
		for (const auto& [id, _] : holdNotes)
		{
			auto it = score.holdNotes.find(id);
			if (it != score.holdNotes.end())
				capture.holdNotes.emplace(id, it->second);
		}
		for (const auto& [key, _] : hispeeds)
			capture.captureHiSpeed(score, key);
		return capture;
	}

	void ScoreCapture::clearNotes()
	{
		notes.clear();
		holdNotes.clear();
	}

	void ScoreCapture::clearHispeeds() { hispeeds.clear(); }

	void ScoreCapture::clear()
	{
		clearNotes();
		clearHispeeds();
	}

	std::optional<ScoreEdit> ScoreCapture::diffChanges(const Score& score) const
	{
		ScoreEdit edit;
		for (const auto& [id, before] : notes)
		{
			auto it = score.notes.find(id);
			if (it == score.notes.end() || before == it->second)
				continue;
			edit.notes().push_back({ id, before, it->second });
		}
		for (const auto& [id, before] : holdNotes)
		{
			auto it = score.holdNotes.find(id);
			if (it == score.holdNotes.end() || before == it->second)
				continue;
			edit.holds().push_back({ id, before, it->second });
		}
		for (const auto& [key, before] : hispeeds)
		{
			if (!isArrayIndexInBounds(key.first, score.layers))
				continue;
			const HiSpeedCollection& hiSpeedChanges = score.layers[key.first].hiSpeedChanges;
			auto it = hiSpeedChanges.find(key.second);
			if (it == hiSpeedChanges.end() || isSame(before, it->second))
				continue;
			edit.hispeeds().push_back({ key, before, it->second });
		}

		if (edit.empty())
			return std::nullopt;
		return edit;
	}

	std::optional<ScoreEdit> ScoreCapture::diffAll(const ScoreCapture& afterCapture) const
	{
		ScoreEdit edit;

		for_each_relations(
		    notes, afterCapture.notes,
		    [&](auto& kvBefore, auto& kvAfter)
		    {
			    if (!(kvBefore.second == kvAfter.second))
				    edit.notes().push_back({ kvBefore.first, kvBefore.second, kvAfter.second });
		    },
		    [&](auto& kvBefore)
		    { edit.notes().push_back({ kvBefore.first, kvBefore.second, std::nullopt }); },
		    [&](auto& kvAfter)
		    { edit.notes().push_back({ kvAfter.first, std::nullopt, kvAfter.second }); });

		for_each_relations(
		    holdNotes, afterCapture.holdNotes,
		    [&](auto& kvBefore, auto& kvAfter)
		    {
			    if (!(kvBefore.second == kvAfter.second))
				    edit.holds().push_back({ kvBefore.first, kvBefore.second, kvAfter.second });
		    },
		    [&](auto& kvBefore)
		    { edit.holds().push_back({ kvBefore.first, kvBefore.second, std::nullopt }); },
		    [&](auto& kvAfter)
		    { edit.holds().push_back({ kvAfter.first, std::nullopt, kvAfter.second }); });

		for_each_relations(
		    hispeeds, afterCapture.hispeeds,
		    [&](auto& kvBefore, auto& kvAfter)
		    {
			    if (!isSame(kvBefore.second, kvAfter.second))
				    edit.hispeeds().push_back({ kvBefore.first, kvBefore.second, kvAfter.second });
		    },
		    [&](auto& kvBefore)
		    { edit.hispeeds().push_back({ kvBefore.first, kvBefore.second, std::nullopt }); },
		    [&](auto& kvAfter)
		    { edit.hispeeds().push_back({ kvAfter.first, std::nullopt, kvAfter.second }); });

		if (edit.empty())
			return std::nullopt;
		return edit;
	}

	void TempoTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		if (value)
			score.tempoChanges.insert_or_assign(key, *value);
		else
			score.tempoChanges.erase(key);
	}

	void TimeSignatureTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		if (value)
			score.timeSignatures.insert_or_assign(key, *value);
		else
			score.timeSignatures.erase(key);
	}

	void SkillTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		// Skills are ordered by tick only, so replacing means erase + insert
		score.skills.erase(Skill{ key });
		if (value)
			score.skills.insert(*value);
	}

	void WaypointTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		if (value)
			score.waypoints.insert_or_assign(key, *value);
		else
			score.waypoints.erase(key);
	}

	void HiSpeedTraits::set(Score& score, const Key& key, const std::optional<Value>& value)
	{
		auto&& [layer, tick] = key;
		// Edits on the layers themselves are responsible for restoring them
		if (!isArrayIndexInBounds(layer, score.layers))
			return;

		HiSpeedCollection& hiSpeedChanges = score.layers[layer].hiSpeedChanges;
		if (value)
			hiSpeedChanges.insert_or_assign(key.second, *value);
		else
			hiSpeedChanges.erase(key.second);
	}

	void ScoreExtensionEdit::undo(HistoryContext& ctx) const
	{
		ctx.score = *score.before;
		ctx.metadata.isExtendedScore = true;
	}

	void ScoreExtensionEdit::redo(HistoryContext& ctx) const
	{
		ctx.score = *score.after;
		ctx.metadata.isExtendedScore = false;
	}

	void LayerEdit::undo(HistoryContext& ctx) const
	{
		ctx.score.layers = layers.before;
		for (const auto& change : notes)
			change.undo(ctx);
	}

	void LayerEdit::redo(HistoryContext& ctx) const
	{
		ctx.score.layers = layers.after;
		for (const auto& change : notes)
			change.redo(ctx);
	}

	LayerCapture::LayerCapture(const Score& score) : layers(score.layers)
	{
		noteLayers.reserve(score.notes.size());
		for (const auto& [id, note] : score.notes)
			noteLayers.emplace(id, note.layer);
	}

	LayerEdit LayerCapture::diff(const Score& score) const
	{
		LayerEdit edit{ { layers, score.layers }, {} };
		for (const auto& [id, layer] : noteLayers)
		{
			auto it = score.notes.find(id);
			if (it == score.notes.end() || it->second.layer == layer)
				continue;
			// Only the layer of a note changes in a layer edit
			Note before = it->second;
			before.layer = layer;
			edit.notes.push_back({ id, before, it->second });
		}
		return edit;
	}

	void FeverEdit::undo(HistoryContext& ctx) const { ctx.score.fever = change.before; }

	void FeverEdit::redo(HistoryContext& ctx) const { ctx.score.fever = change.after; }

	void undoEdit(const HistoryEdit& edit, HistoryContext& ctx)
	{
		std::visit([&ctx](const auto& e) { e.undo(ctx); }, edit);
	}

	void redoEdit(const HistoryEdit& edit, HistoryContext& ctx)
	{
		std::visit([&ctx](const auto& e) { e.redo(ctx); }, edit);
	}
}

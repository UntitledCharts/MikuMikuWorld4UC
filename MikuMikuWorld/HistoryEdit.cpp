#include "HistoryEdit.h"
#include <algorithm>

namespace MikuMikuWorld
{
	namespace
	{
		// Both notes are the same note of the same score here (found by the same ID),
		// so unlike isSame() the relationships are part of the comparison too
		static bool unchanged(const Note& a, const Note& b)
		{
			return isSame(a, b) && a.holdID == b.holdID;
		}

		bool unchanged(const HoldNote& a, const HoldNote& b)
		{
			return a.fadeType == b.fadeType && a.steps == b.steps && a.joints == b.joints &&
			       a.separators.size() == b.separators.size() &&
			       std::equal(a.separators.begin(), a.separators.end(), b.separators.begin(),
			                  [](const HoldNoteStep& x, const HoldNoteStep& y)
			                  { return x.ID == y.ID && isSame(x, y); });
		}
	}

	void ChangeMetadata::undo(HistoryContext& ctx) const
	{
		std::visit([&](const auto& c) { ctx.metadata.*(c.field) = c.change.before; }, change);
	}

	void ChangeMetadata::redo(HistoryContext& ctx) const
	{
		std::visit([&](const auto& c) { ctx.metadata.*(c.field) = c.change.after; }, change);
	}

	void ChangeNotes::undo(HistoryContext& ctx) const
	{
		for (const auto& c : notes)
			ctx.score.notes.at(c.id) = c.before;
		for (const auto& c : holds)
			ctx.score.holdNotes.at(c.id) = c.before;
	}

	void ChangeNotes::redo(HistoryContext& ctx) const
	{
		for (const auto& c : notes)
			ctx.score.notes.at(c.id) = c.after;
		for (const auto& c : holds)
			ctx.score.holdNotes.at(c.id) = c.after;
	}

	void ChangeNote::undo(HistoryContext& ctx) const
	{
		ctx.score.notes.at(change.id) = change.before;
	}

	void ChangeNote::redo(HistoryContext& ctx) const
	{
		ctx.score.notes.at(change.id) = change.after;
	}

	void NotesCapture::captureNote(const NotesContext& context, id_t noteID)
	{
		if (notes.count(noteID))
			return;
		auto it = context.notes.find(noteID);
		if (it != context.notes.end())
			notes.emplace(noteID, it->second);
	}

	void NotesCapture::captureHold(const NotesContext& context, id_t holdID)
	{
		if (holdNotes.count(holdID))
			return;
		auto it = context.holdNotes.find(holdID);
		if (it == context.holdNotes.end())
			return;
		const HoldNote& hold = it->second;
		holdNotes.emplace(holdID, hold);
		for (id_t step : hold.steps)
			captureNote(context, step);
	}

	void NotesCapture::clear()
	{
		notes.clear();
		holdNotes.clear();
	}

	std::optional<NotesEdit> NotesCapture::diff(const NotesContext& context) const
	{
		ChangeNotes edit;
		for (const auto& [id, before] : notes)
		{
			auto it = context.notes.find(id);
			if (it != context.notes.end() && !unchanged(before, it->second))
				edit.notes.push_back({ id, before, it->second });
		}
		for (const auto& [id, before] : holdNotes)
		{
			auto it = context.holdNotes.find(id);
			if (it != context.holdNotes.end() && !unchanged(before, it->second))
				edit.holds.push_back({ id, before, it->second });
		}

		if (edit.empty())
			return std::nullopt;
		// Use single edit if there's only 1 note
		if (edit.notes.size() == 1 && edit.holds.empty())
			return ChangeNote{ std::move(edit.notes.front()) };
		return std::move(edit);
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
		// Edits on the layers themselves are responsible for restoring them
		if (key.first < 0 || key.first >= static_cast<id_t>(score.layers.size()))
			return;

		HiSpeedCollection& collection = score.layers[key.first].hiSpeedChanges;
		if (value)
			collection.insert_or_assign(key.second, *value);
		else
			collection.erase(key.second);
	}

	void ChangeFever::undo(HistoryContext& ctx) const { ctx.score.fever = change.before; }

	void ChangeFever::redo(HistoryContext& ctx) const { ctx.score.fever = change.after; }

	void HiSpeedCapture::capture(const Score& score, const layered_tick_t& key)
	{
		if (hispeeds.count(key) || key.first < 0 ||
		    key.first >= static_cast<id_t>(score.layers.size()))
			return;

		const HiSpeedCollection& collection = score.layers[key.first].hiSpeedChanges;
		auto it = collection.find(key.second);
		if (it != collection.end())
			hispeeds.emplace(key, it->second);
	}

	void HiSpeedCapture::clear() { hispeeds.clear(); }

	std::optional<HiSpeedsEdit> HiSpeedCapture::diff(const Score& score) const
	{
		ChangeHiSpeeds edit;
		for (const auto& [key, before] : hispeeds)
		{
			if (key.first < 0 || key.first >= static_cast<id_t>(score.layers.size()))
				continue;
			const HiSpeedCollection& collection = score.layers[key.first].hiSpeedChanges;
			auto it = collection.find(key.second);
			if (it != collection.end() && !isSame(before, it->second))
				edit.entries.push_back({ key, before, it->second });
		}

		if (edit.empty())
			return std::nullopt;
		if (edit.entries.size() == 1)
		{
			auto& entry = edit.entries.front();
			return ChangeHiSpeed{ entry.key, std::move(entry.before), std::move(entry.after) };
		}
		return std::move(edit);
	}

	void MoveToLayer::undo(HistoryContext& ctx) const
	{
		hispeeds.undo(ctx);
		notes.undo(ctx);
	}

	void MoveToLayer::redo(HistoryContext& ctx) const
	{
		notes.redo(ctx);
		hispeeds.redo(ctx);
	}

	void undoEdit(const HistoryEdit& edit, HistoryContext& ctx)
	{
		std::visit([&ctx](const auto& e) { e.undo(ctx); }, edit);
	}

	void redoEdit(const HistoryEdit& edit, HistoryContext& ctx)
	{
		std::visit([&ctx](const auto& e) { e.redo(ctx); }, edit);
	}
}

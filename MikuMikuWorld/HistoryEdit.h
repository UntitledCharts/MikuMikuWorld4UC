#pragma once
#include "Score.h"
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <memory>

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

	// Immutable selection snapshot. Edits made on the same selection share one object
	// nullptr means nothing is selected
	using SelectionRef = std::shared_ptr<const ScoreSelection>;
	using SelectionChange = FieldChange<SelectionRef>;

	// ---- Metadata ----------------------------------------------------------------------------
	template <typename T> struct MetadataChange
	{
		T ScoreMetadata::* field;
		FieldChange<T> change;
	};

	using MetadataFieldChange = std::variant<MetadataChange<std::string>, MetadataChange<float>,
	                                         MetadataChange<int>, MetadataChange<bool>>;

	struct MetadataEdit
	{
		MetadataFieldChange change;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// ---- Keyed collections
	// Traits provide Key, Value and `static void set(Score&, const Key&, const optional<Value>&)`
	// which inserts/replaces the entry or erases it if the value is empty

	// One entry. Covers insert (no before), change (both) and erase (no after)
	template <typename Traits> struct ChangeEntry
	{
		using Key = typename Traits::Key;
		using Value = typename Traits::Value;

		Key key;
		std::optional<Value> before;
		std::optional<Value> after;

		void undo(HistoryContext& ctx) const { Traits::set(ctx.score, key, before); }
		void redo(HistoryContext& ctx) const { Traits::set(ctx.score, key, after); }
	};

	template <typename Traits> struct SingleEdit
	{
		using Key = typename Traits::Key;
		using Value = typename Traits::Value;
		ChangeEntry<Traits> change;

		SingleEdit(const ChangeEntry<Traits>& c) : change(c) {}
		SingleEdit(ChangeEntry<Traits>&& c) : change(std::move(c)) {}
		SingleEdit(Key key, std::optional<Value> before, std::optional<Value> after)
		    : change{ std::move(key), std::move(before), std::move(after) }
		{
		}

		void undo(HistoryContext& ctx) const { change.undo(ctx); }
		void redo(HistoryContext& ctx) const { change.redo(ctx); }
	};

	template <typename Traits> struct MultiEdit
	{
		std::vector<ChangeEntry<Traits>> changes;

		void undo(HistoryContext& ctx) const
		{
			for (auto it = changes.rbegin(); it != changes.rend(); ++it)
				it->undo(ctx);
		}
		void redo(HistoryContext& ctx) const
		{
			for (const auto& change : changes)
				change.redo(ctx);
		}
	};

	struct NoteTraits
	{
		using Key = id_t;
		using Value = Note;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};
	struct HoldTraits
	{
		using Key = id_t;
		using Value = HoldNote;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};
	struct HiSpeedTraits
	{
		using Key = layered_tick_t;
		using Value = HiSpeed;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};
	struct TempoTraits
	{
		using Key = tick_t;
		using Value = Tempo;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};
	struct TimeSignatureTraits
	{
		using Key = measure_t;
		using Value = TimeSignature;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};
	struct SkillTraits
	{
		using Key = tick_t;
		using Value = Skill;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};
	struct WaypointTraits
	{
		using Key = id_t;
		using Value = Waypoint;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};

	using SingleNoteEdit = SingleEdit<NoteTraits>;
	using SingleHoldEdit = SingleEdit<HoldTraits>;
	using SingleHiSpeedEdit = SingleEdit<HiSpeedTraits>;
	using SingleTempoEdit = SingleEdit<TempoTraits>;
	using SingleTimeSignatureEdit = SingleEdit<TimeSignatureTraits>;
	using SingleSkillEdit = SingleEdit<SkillTraits>;
	using SingleWaypointEdit = SingleEdit<WaypointTraits>;

	using MultiNoteEdit = MultiEdit<NoteTraits>;
	using MultiHoldEdit = MultiEdit<HoldTraits>;
	using MultiHiSpeedEdit = MultiEdit<HiSpeedTraits>;

	struct ScoreEdit : protected MultiNoteEdit, protected MultiHoldEdit, protected MultiHiSpeedEdit
	{
		auto& notes() { return MultiNoteEdit::changes; }
		auto& holds() { return MultiHoldEdit::changes; }
		auto& hispeeds() { return MultiHiSpeedEdit::changes; }

		bool empty() const;
		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
		void merge(ScoreEdit&& other);
	};

	struct FeverEdit
	{
		FieldChange<Fever> change;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	struct ScoreExtensionEdit
	{
		// boxed to keep HistoryEdit small.
		FieldChange<std::unique_ptr<const Score>> score;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// Created, merged, reordered, hidden and renamed in the layer window.
	struct LayerEdit
	{
		FieldChange<LayerCollection> layers;
		std::vector<ChangeEntry<NoteTraits>> notes;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// Every alternative must provide:
	// void undo(HistoryContext&) const;
	// void redo(HistoryContext&) const;
	using HistoryEdit =
	    std::variant<MetadataEdit, SingleNoteEdit, SingleHoldEdit, SingleHiSpeedEdit,
	                 SingleTempoEdit, SingleTimeSignatureEdit, SingleSkillEdit, SingleWaypointEdit,
	                 FeverEdit, ScoreEdit, ScoreExtensionEdit, LayerEdit>;

	// Convert to HistoryEdit, collapse to Single*Edit variance when possible
	HistoryEdit toHistoryEdit(ScoreEdit&& edit);

	// Use to capture what an edit does to the score(notes, holds, hispeeds)
	class ScoreCapture
	{
	  public:
		void captureNote(const NotesContext& context, id_t noteID);
		// Captures the hold and, with steps, all of its steps
		void captureHold(const NotesContext& context, id_t holdID, bool steps = true);
		void captureHiSpeed(const Score& score, const layered_tick_t& key);
		// Captures the notes and holds created by an edit, IDs are never reused
		void captureCreated(const Score& score, id_t beforeNoteID, id_t afterNoteID,
		                    id_t beforeHoldID, id_t afterHoldID);
		// Adds everything captured by other that is not captured here
		void merge(const ScoreCapture& other);
		// Captures the current state of exactly the objects that are captured here.
		// Objects that no longer exist are not captured
		ScoreCapture recapture(const Score& score) const;

		void clear();
		void clearNotes();
		void clearHispeeds();

		// compute the changes between the captured state and the current score
		// nullopt if nothing was changed
		std::optional<ScoreEdit> diffChanges(const Score& score) const;
		// compute the edits(change/add/erase) between the captured state and the after state
		// nullopt if nothing was edited
		// unlike diffChanges():
		// a captured object that no longer exists counts as erased
		// an object that exists now but wasn't captured before counts as inserted
		std::optional<ScoreEdit> diffAll(const ScoreCapture& afterCapture) const;

	  private:
		NoteCollection notes;
		HoldNoteCollection holdNotes;
		std::map<layered_tick_t, HiSpeed> hispeeds;
	};

	// What a layer edit may change: the layers and the layer of every note
	class LayerCapture
	{
	  public:
		explicit LayerCapture(const Score& score);
		// The edit from the captured state to the current score
		LayerEdit diff(const Score& score) const;

	  private:
		LayerCollection layers;
		std::unordered_map<id_t, id_t> noteLayers;
	};

	void undoEdit(const HistoryEdit& edit, HistoryContext& ctx);
	void redoEdit(const HistoryEdit& edit, HistoryContext& ctx);
}

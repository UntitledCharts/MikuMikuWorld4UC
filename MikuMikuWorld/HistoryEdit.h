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

	struct ChangeMetadata
	{
		MetadataFieldChange change;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// ---- Notes -------------------------------------------------------------------------------

	struct NoteChange
	{
		id_t id;
		Note before;
		Note after;
	};

	struct HoldChange
	{
		id_t id;
		HoldNote before;
		HoldNote after;
	};

	struct ChangeNotes
	{
		std::vector<NoteChange> notes;
		std::vector<HoldChange> holds;

		bool empty() const { return notes.empty() && holds.empty(); }
		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// Exactly one note changed
	struct ChangeNote
	{
		NoteChange change;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	using NotesEdit = std::variant<ChangeNote, ChangeNotes>;

	// The state of some notes at a point in time.
	//
	// Notes that no longer exist are ignored
	// Inserting and erasing notes has its own edit types.
	class NotesCapture : protected NotesContext
	{
	  public:
		void captureNote(const NotesContext& context, id_t noteID);
		void captureHold(const NotesContext& context, id_t holdID);
		void clear();
		// compute the difference between captured notes and current notes
		// nullopt if nothing changed
		std::optional<NotesEdit> diff(const NotesContext& context) const;
	};

	// ---- Score events ------------------------------------------------------------------------

	// One entry of a keyed collection (tempo, time signature, skill, waypoint, hi-speed).
	// Covers all three edits: insert (no before), change (both) and erase (no after)
	template <typename K, typename V> struct EntryChange
	{
		K key;
		std::optional<V> before;
		std::optional<V> after;
	};

	// Traits provide Key, Value and `static void set(Score&, const Key&, const optional<Value>&)`
	// which inserts/replaces the entry or erases it if the value is empty

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

	// Multiple entries, for edits that can affect a group of entries
	template <typename Traits> struct ChangeEntries
	{
		using Key = typename Traits::Key;
		using Value = typename Traits::Value;
		std::vector<EntryChange<Key, Value>> entries;

		bool empty() const { return entries.empty(); }

		void undo(HistoryContext& ctx) const
		{
			for (auto it = entries.rbegin(); it != entries.rend(); ++it)
				Traits::set(ctx.score, it->key, it->before);
		}

		void redo(HistoryContext& ctx) const
		{
			for (const auto& entry : entries)
				Traits::set(ctx.score, entry.key, entry.after);
		}
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
	struct HiSpeedTraits
	{
		using Key = layered_tick_t;
		using Value = HiSpeed;
		static void set(Score& score, const Key& key, const std::optional<Value>& value);
	};

	using ChangeTempo = ChangeEntry<TempoTraits>;
	using ChangeTimeSignature = ChangeEntry<TimeSignatureTraits>;
	using ChangeSkill = ChangeEntry<SkillTraits>;
	using ChangeWaypoint = ChangeEntry<WaypointTraits>;
	using ChangeHiSpeed = ChangeEntry<HiSpeedTraits>;
	using ChangeHiSpeeds = ChangeEntries<HiSpeedTraits>;

	using HiSpeedsEdit = std::variant<ChangeHiSpeed, ChangeHiSpeeds>;

	struct ChangeFever
	{
		FieldChange<Fever> change;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// The state of some hi-speeds at a point in time (see NotesCapture).
	// Hi-speeds that no longer exist are ignored.
	class HiSpeedCapture
	{
	  public:
		void capture(const Score& score, const layered_tick_t& key);
		void clear();
		// compute the difference between captured hi-speeds and current hi-speeds
		// nullopt if nothing changed
		std::optional<HiSpeedsEdit> diff(const Score& score) const;

	  private:
		std::map<layered_tick_t, HiSpeed> hispeeds;
	};

	// Moving the selection to another layer changes the layer of the notes and moves the
	// hi-speeds into another layer's collection. It must undo as a whole
	struct MoveToLayer
	{
		ChangeNotes notes;
		ChangeHiSpeeds hispeeds;

		void undo(HistoryContext& ctx) const;
		void redo(HistoryContext& ctx) const;
	};

	// Every alternative must provide:
	// void undo(HistoryContext&) const;
	// void redo(HistoryContext&) const;
	using HistoryEdit = std::variant<ChangeMetadata, ChangeNote, ChangeNotes, ChangeHiSpeed,
	                                 ChangeHiSpeeds, ChangeTempo, ChangeTimeSignature, ChangeSkill,
	                                 ChangeWaypoint, ChangeFever, MoveToLayer>;

	// Widens one of the diff() results into a HistoryEdit
	template <typename... Ts> HistoryEdit toHistoryEdit(std::variant<Ts...>&& edit)
	{
		return std::visit([](auto&& e) -> HistoryEdit { return std::move(e); }, std::move(edit));
	}

	void undoEdit(const HistoryEdit& edit, HistoryContext& ctx);
	void redoEdit(const HistoryEdit& edit, HistoryContext& ctx);
}

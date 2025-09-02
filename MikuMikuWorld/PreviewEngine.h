#pragma once
#include <array>
#include <vector>
#include <map>
#include <memory>
#include "Math.h"
#include "Score.h"
#include "Rendering/Texture.h"
#include "Rendering/Sprite.h"
#include "DirectXMath.h"

namespace MikuMikuWorld
{
	enum class SpriteType : int
	{
		NoteLeft,
		NoteMiddle,
		NoteRight,
		TraceDiamond,
		FlickArrowLeft,
		FlickArrowUp = FlickArrowLeft + 6,
		SimultaneousLine = FlickArrowUp + 6,
		HoldTick,
		Slot,
		SlotGlow
	};

	enum class SpriteLayer : uint8_t
	{
		SLOT_GLOW_EFFECT,
		PARTICLE_EFFECT,
		FLICK_ARROW,
		DIAMOND,
		BASE_NOTE,
		TICK_NOTE,
		HOLD_PATH,
		UNDER_NOTE_EFFECT
	};

	enum class ParticleEffectType : uint32_t
	{
		Lane,
		NoteTapLane,
		NoteCriticalLane,
		NoteCriticalFlickLane,

		NoteTapCircular,
		NoteTapLinear,

		NoteLongCircular,
		NoteLongLinear,

		NoteFlickCircular,
		NoteFlickLinear,
		NoteFlickDirectional,

		NoteFrictionCircular,
		NoteFrictionLinear,

		NoteCriticalCircular,
		NoteCriticalLinear,

		NoteLongCriticalCircular,
		NoteLongCriticalLinear,

		NoteCriticalFlickCircular,
		NoteCriticalFlickLinear,
		NoteCriticalDirectional,

		NoteFrictionCriticalCircular,
		NoteFrictionCriticalLinear,

		NoteLongAmongCircular,
		NoteLongAmongCriticalCircular,

		NoteLongSegmentCircular,
		NoteLongSegmentCircularEx,
		NoteLongSegmentLinear,

		NoteLongCriticalSegmentCircular,
		NoteLongCriticalSegmentCircularEx,
		NoteLongCriticalSegmentLinear,

		SlotNoteTap,
		SlotNoteCritical,
		SlotNoteFlick,
		SlotNoteLong,

		SlotGlowNoteTap,
		SlotGlowNoteCritical,
		SlotGlowNoteFlick,
		SlotGlowNoteLong,

		SlotGlowNoteLongSegment,
		SlotGlowNoteLongCriticalSegment,

		Invalid = UINT32_MAX
	};
}

namespace MikuMikuWorld::Engine
{
	 constexpr float STAGE_LANE_TOP = 47;
	 constexpr float STAGE_LANE_BOTTOM = 803;
	 constexpr float STAGE_LANE_HEIGHT = 850;
	 constexpr float STAGE_LANE_WIDTH = 1420;
	 constexpr float STAGE_NUM_LANES = 12;
	 constexpr float STAGE_TEX_WIDTH = 2048;
	 constexpr float STAGE_TEX_HEIGHT = 1176;
	 constexpr float STAGE_NOTE_HEIGHT = 75;
	 constexpr float STAGE_TARGET_WIDTH = 1920;
	 constexpr float STAGE_TARGET_HEIGHT = 1080;
	 constexpr float STAGE_ASPECT_RATIO = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
	 constexpr float STAGE_ZOOM = 927 / 800.f; // Magic value
	 constexpr float BACKGROUND_SIZE = 2462.25;
}
#include "ScorePreview.h"
//#include "PreviewEngine.h"
#include "Rendering/Camera.h"
#include "ResourceManager.h"
#include "Colors.h"
#include "ImageCrop.h"
#include "Application.h"
#include "ApplicationConfiguration.h"

namespace MikuMikuWorld
{
	namespace Utils
	{
		// Scale a rectangle with specified aspect ratio to be visible inside the target rectangle
		inline void fitRect(float target_width, float target_height,
		                    long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width = target_aspect_ratio > source_aspect_ratio ? source_aspect_ratio * target_height
			                                                  : target_width;
			height = target_aspect_ratio < source_aspect_ratio ? target_width / source_aspect_ratio
			                                                   : target_height;
			return;
		}

		// Scale a rectangle with specified aspect ratio so it fill the area of the target rectangle
		inline void fillRect(float target_width, float target_height,
		                     long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width = target_aspect_ratio < source_aspect_ratio ? source_aspect_ratio * target_height
			                                                  : target_width;
			height = target_aspect_ratio > source_aspect_ratio ? target_width / source_aspect_ratio
			                                                   : target_height;
			return;
		}
	};

	static const int NOTE_SIDE_WIDTH = 91;
	static const int NOTE_SIDE_PAD = 10;
	static const int MAX_FLICK_SPRITES = 6;
	static const int HOLD_XCUTOFF = 36;
	static const int GUIDE_XCUTOFF = 3;
	static const int GUIDE_Y_TOP_CUTOFF = -41;
	static const int GUIDE_Y_BOTTOM_CUTOFF = -12;
	static Color defaultTint{ 1.f, 1.f, 1.f, 1.f };

	void ScorePreviewWindow::drawStage(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		const float stageWidth =
		    (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		const float stageLeft = -stageWidth / 2;
		const float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = Engine::STAGE_TEX_HEIGHT / Engine::STAGE_LANE_HEIGHT;
		renderer->drawRectangle({ stageLeft, stageTop }, { stageWidth, stageHeight }, stage,
		                        stageSprite.getX1(), stageSprite.getX2(), stageSprite.getY1(),
		                        stageSprite.getY2(), defaultTint.scaleAlpha(config.pvStageOpacity),
		                        -1);
	}
}
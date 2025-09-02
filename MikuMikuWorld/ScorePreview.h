#pragma once
#include "ScoreContext.h"
#include "ScoreEditorTimeline.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/Renderer.h"

namespace MikuMikuWorld
{
	class ScorePreviewBackground
	{
		std::string backgroundFile;
		std::string jacketFile;
		Framebuffer framebuffer;
		bool brightness;
		bool init;

		struct DefaultJacket
		{
			static std::array<DirectX::XMFLOAT4, 4> getLeftUV();
			static std::array<DirectX::XMFLOAT4, 4> getRightUV();
			static std::array<DirectX::XMFLOAT4, 4> getLeftMirrorUV();
			static std::array<DirectX::XMFLOAT4, 4> getRightMirrorUV();
			static std::array<DirectX::XMFLOAT4, 4> getCenterUV();
			static std::array<DirectX::XMFLOAT4, 4> getMirrorCenterUV();
		};
		void updateDrawDefaultJacket(Renderer* renderer, const Jacket& jacket);
		public:
		ScorePreviewBackground();
		  ~ScorePreviewBackground();

		void setBrightness(float value);
		void update(Renderer* renderer, const Jacket& jacket);
		bool shouldUpdate(const Jacket& jacket) const;
		void draw(Renderer* renderer, float scrWidth, float scrHeight) const;
	};

	class ScorePreviewWindow
	{
		Framebuffer previewBuffer;
		ScorePreviewBackground background;
		float scaledAspectRatio;
		std::unique_ptr<Texture> notesTex;
		const Texture& getNoteTexture();
		std::pair<float, float> getNoteBound(const Note& note) const;
		std::pair<float, float> getHoldStepBound(const Note& note, const Score& score) const;
		std::pair<float, float> getHoldSegmentBound(const Note& note, const Score& score,
		                                            int curTick) const;

		public:
		ScorePreviewWindow();
		~ScorePreviewWindow();
		void drawStage(Renderer* renderer);
	};
}
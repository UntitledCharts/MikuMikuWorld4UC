#pragma once
#include "SUS.h"
#include <regex>
#include <string>

namespace MikuMikuWorld
{
	class SusDataLine
	{
	  private:
		int measureOffset{};
		int measure{};

	  public:
		std::string header{};
		std::string data{};

		SusDataLine(int measureOffset, const std::string& line);

		inline constexpr int getEffectiveMeasure() const { return measureOffset + measure; }
	};

	class SusParser
	{
	  private:
		int ticksPerBeat;
		int measureOffset;
		float waveOffset;
		std::string title;
		std::string artist;
		std::string designer;
		std::map<std::string, float> bpmDefinitions;
		std::vector<Bar> bars;

		bool isCommand(const std::string& line);
		int getTicks(int measure, int i, int total);

		SUSNoteStream getNoteStream(const std::vector<SUSNote>& stream);
		std::vector<SUSNote> getNotes(const SusDataLine& line);
		std::vector<SUSBPM> getBpms(const std::vector<SusDataLine>& bpmLines);
		std::vector<Bar> getBars(const std::vector<BarLength>& barLengths);
		std::vector<SUSHiSpeed> getHiSpeeds(const std::vector<SusDataLine>& hiSpeeds);

	  public:
		SusParser();

		SUS parse(const std::string& filename);
		void processCommand(std::string& line);
	};
}
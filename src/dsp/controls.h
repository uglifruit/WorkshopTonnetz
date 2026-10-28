// controls.h — the three knobs across three pages, with pickup.
//
// Switch (verified in ComputerCard.h and on the sibling cards): Middle and Up
// are stable; DOWN is spring-loaded and returns to Middle on release.
//
//   Middle       PERFORM    Main wander   X glide      Y anticipation
//   Up           TIMING     Main BPM      X density    Y phrase length
//   Down (held)  ADVANCED   Main CV pull  X rhythm     Y inertia
//
// A Down TAP — released within 400 ms with no knob moved — is a manual reset
// (same as Pulse In 2). The decision is made on release, so nothing timed is
// layered over playing.
//
// PICKUP: on arriving at a page each knob is "waiting" until it reaches (or
// crosses) the value that page last had, so changing page never makes the
// other page's parameters jump. While a knob waits its LED (0 Main, 1 X, 2 Y)
// pulses — WorkshopShepard found that pickup without feedback reads as "the
// knobs are dead". PERFORM is live from boot.
//
// Ticked at the control rate (3 kHz); all durations are in control ticks.

#pragma once
#include <stdint.h>
#include "dsp_config.h"
#include "platform.h"

namespace tonnetz {

class Controls
{
public:
	enum Page : uint8_t { kPerform = 0, kTiming = 1, kAdvanced = 2 };
	enum Sw : uint8_t { kDown = 0, kMiddle = 1, kUp = 2 };   // ComputerCard::Switch order

	void Init();
	void Tick(const int32_t knob[3], int sw);

	Page    CurrentPage() const { return page_; }
	int32_t Value(Page p, int k) const { return stored_[p][k]; }
	bool    Waiting(int k) const { return !live_[page_][k]; }
	int     PhraseIndex() const { return phraseIndex_; }

	/// True once per Down tap.
	bool TakeReset() { const bool r = resetRequest_; resetRequest_ = false; return r; }

private:
	void Arrive(Page p, const int32_t knob[3]);

	Page    page_ = kPerform;
	int32_t stored_[3][3];
	bool    live_[3][3];
	int8_t  arrivalSign_[3][3];

	int  lastSw_ = kMiddle;
	int  switchSettle_ = 0;
	int  pendingPage_ = -1;
	int  pageSettle_ = 0;

	int     downTicks_ = 0;
	bool    downMoved_ = false;
	int32_t downArrival_[3] = { 0, 0, 0 };
	bool    resetRequest_ = false;

	int phraseIndex_ = 2;
	int32_t lastKnob_[3] = { 0, 0, 0 };
};

} // namespace tonnetz

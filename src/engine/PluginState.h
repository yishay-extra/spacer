#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <map>
#include <optional>
#include <string>

#include "core/PluginSlot.h"

namespace spacer::pluginstate {

/// Plugin state <-> PluginSlot (SPEC §13: `{pluginId, stateBase64, params}`).
/// Message thread only (plugins may lock or allocate in get/setState).

/// Standard (RFC 4648) base64, not juce::MemoryBlock::toBase64Encoding().
std::string encode(const juce::MemoryBlock& data);
/// nullopt for invalid base64. Empty input decodes to an empty block.
std::optional<juce::MemoryBlock> decode(const std::string& base64);

/// The processor's current state blob, encoded.
std::string capture(juce::AudioProcessor& processor);

/// Applies `params` (index -> normalised value); indices the processor
/// does not have are ignored. Returns how many were applied.
int applyParams(juce::AudioProcessor& processor, const std::map<int, float>& params);

/// The slot's state (if any) then its params. False (with `error`) if the
/// state is not valid base64; params are applied regardless.
bool restore(juce::AudioProcessor& processor, const PluginSlot& slot, std::string* error = nullptr);

/// Current normalised values of the parameters listed in `params` (keys kept,
/// values read back; unknown indices dropped).
std::map<int, float> readParams(const juce::AudioProcessor& processor, const std::map<int, float>& params);

}  // namespace spacer::pluginstate

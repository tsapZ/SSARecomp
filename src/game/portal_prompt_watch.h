#pragma once

// Opens the portal menu when the game asks a player for a Skylander ("Spieler 1: Bitte setze
// einen Skylander auf das Portal of Power"), and closes it again if the game stops waiting before
// a figure was chosen. Reads the game's portal prompt object from guest memory; see
// portal_prompt_watch.cpp.

namespace rex::memory {
class Memory;
}  // namespace rex::memory

namespace ssarecomp {

// Starts the watcher thread. Call once, after the runtime is set up.
void StartPortalPromptWatch(rex::memory::Memory* memory);

// Stops and joins it; call before the runtime (and its guest memory) goes away.
void StopPortalPromptWatch();

// True while the game runs its figure sequence (asking for a Skylander, bringing one in): the
// prompt object's mode is not 0, as of the last poll.
bool PortalPromptActive();

}  // namespace ssarecomp

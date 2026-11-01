#pragma once

// Call before anything opens user files: blocks until the instance that
// restarted us (--ssx_wait_pid) has exited.
void SsxWaitForPreviousInstance();

// Starts a new instance with the same arguments; the caller then quits.
bool SsxSpawnRestart();

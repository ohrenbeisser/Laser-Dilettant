#pragma once

// Registers the resources compiled into the static core library. Must be
// called once before resources are accessed (main(), tests).
void ldInitResources();

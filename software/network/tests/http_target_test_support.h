#pragma once
#include "http_target.h"

// Completes the deferred command before the caller tests publication.
void run_target_command(HttpTarget&, Message*, Message**, Message**);

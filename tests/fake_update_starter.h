#pragma once

#include "update/updater.h"

// Records start() calls; the test decides how each job ends. No Q_OBJECT:
// it adds no signals or slots, and tests do not run moc over headers.
class FakeUpdateStarter final : public pi_control::update::UpdateStarter {

public:
    void start() override { ++starts; }
    void finish(Result result, const QString& detail = {}) { emit finished(result, detail); }

    int starts = 0;
};

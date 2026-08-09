#include "xiaoxin_overview_authority_state.h"

#include <cassert>
#include <iostream>

int main() {
    XiaoxinOverviewAuthorityState state;
    assert(state.IsConfigured() == false);
    assert(state.Allows(XiaoxinOverviewSource::kMqtt));
    assert(!state.Allows(XiaoxinOverviewSource::kWebSocket));

    state.Configure(true);
    assert(state.IsConfigured());

    assert(state.Allows(XiaoxinOverviewSource::kMqtt));
    assert(!state.Allows(XiaoxinOverviewSource::kWebSocket));
    assert(state.IsNewMqttRevision(10));
    state.CommitMqttRevision(10);
    assert(state.last_overview_revision_ == 10);

    assert(!state.Allows(XiaoxinOverviewSource::kWebSocket));
    assert(state.last_overview_revision_ == 10);
    assert(!state.IsNewMqttRevision(10));
    assert(state.IsNewMqttRevision(11));

    state.Configure(false);
    assert(state.Allows(XiaoxinOverviewSource::kWebSocket));
    assert(state.last_overview_revision_ == 10);

    std::cout << "xiaoxin overview authority state tests passed\n";
    return 0;
}

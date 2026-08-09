#ifndef XIAOXIN_OVERVIEW_AUTHORITY_STATE_H
#define XIAOXIN_OVERVIEW_AUTHORITY_STATE_H

enum class XiaoxinOverviewSource {
    kWebSocket,
    kMqtt,
};

struct XiaoxinOverviewAuthorityState {
    bool configured_ = false;
    bool mqtt_overview_authoritative_ = false;
    int last_overview_revision_ = 0;

    void Configure(bool mqtt_authoritative) {
        configured_ = true;
        mqtt_overview_authoritative_ = mqtt_authoritative;
    }

    bool IsConfigured() const { return configured_; }

    bool Allows(XiaoxinOverviewSource source) const {
        return source == XiaoxinOverviewSource::kMqtt ||
               (configured_ && !mqtt_overview_authoritative_);
    }

    bool IsNewMqttRevision(int revision) const {
        return revision > last_overview_revision_;
    }

    void CommitMqttRevision(int revision) {
        last_overview_revision_ = revision;
    }
};

#endif  // XIAOXIN_OVERVIEW_AUTHORITY_STATE_H

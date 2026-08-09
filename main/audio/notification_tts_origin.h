#ifndef NOTIFICATION_TTS_ORIGIN_H
#define NOTIFICATION_TTS_ORIGIN_H

#include <cstdint>

// Application serializes this token owner with tts_control_mutex_. A stale
// close/continuation can only clear the token it originally received.
class NotificationTtsOrigin {
public:
    using Token = uint64_t;

    Token BeginOpenIntent() {
        current_token_ = ++next_token_;
        return current_token_;
    }

    bool HasOpenIntent() const {
        return current_token_ != 0;
    }

    bool IsCurrent(Token token) const {
        return token != 0 && token == current_token_;
    }

    bool ConsumeForTtsStart() {
        if (current_token_ == 0) return false;
        current_token_ = 0;
        return true;
    }

    bool ClearOpenIntent(Token token) {
        if (!IsCurrent(token)) return false;
        current_token_ = 0;
        return true;
    }

private:
    Token next_token_ = 0;
    Token current_token_ = 0;
};

#endif

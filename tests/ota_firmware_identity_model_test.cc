#include <assert.h>
#include <stdio.h>

#include "ota.h"

int main() {
    const FirmwareOfferTarget target{
        "esp32-s3-touch-lcd-1.46",
        "esp32-s3-touch-lcd-1.46",
        "xiaoxin-ota-16m-v1",
    };

    FirmwareOffer legacy_offer;
    assert(legacy_offer.MatchesTarget(target));

    FirmwareOffer verified_offer;
    verified_offer.has_extended_fields = true;
    verified_offer.model = target.model;
    verified_offer.board_type = target.board_type;
    verified_offer.partition_layout_id = target.partition_layout_id;
    assert(verified_offer.MatchesTarget(target));

    verified_offer.model = "wrong-model";
    assert(!verified_offer.MatchesTarget(target));
    verified_offer.model = target.model;

    verified_offer.board_type = "wrong-board";
    assert(!verified_offer.MatchesTarget(target));
    verified_offer.board_type = target.board_type;

    verified_offer.partition_layout_id = "wrong-layout";
    assert(!verified_offer.MatchesTarget(target));

    puts("ota firmware identity model tests passed");
    return 0;
}

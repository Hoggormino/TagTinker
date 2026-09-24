/*
 * Host unit test for the pure protocol/layout helpers.
 *
 * Covers the geometry, page-mapping and profile helpers that are defined
 * inline in protocol/tagtinker_proto.h. These are pure functions of their
 * arguments (no Flipper SDK, no app state), so the test just includes the
 * real header and checks exact values. Run via test/run_proto_test.sh.
 * Exit code 0 = all pass.
 *
 * The .c-defined functions (tagtinker_crc16, barcode validation, frame
 * builders) are intentionally NOT covered here: tagtinker_proto.c includes
 * ../tagtinker_app.h and so cannot be compiled off-device without stubbing
 * the whole app. That is a separate follow-up.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "tagtinker_proto.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(name, cond)                                \
    do {                                                 \
        int ok = (cond);                                 \
        if(!ok) fails++;                                 \
        printf("  %s %s\n", ok ? "ok  " : "FAIL", name); \
    } while(0)

int main(void) {
    printf("wire<->glass constants:\n");
    /* Color 2.6 wire image is portrait 152x296; glass is landscape 296x152. */
    CHECK("wire is 152x296", TAGTINKER_COLOR26_WIRE_W == 152U && TAGTINKER_COLOR26_WIRE_H == 296U);
    CHECK(
        "glass is 296x152", TAGTINKER_COLOR26_GLASS_W == 296U && TAGTINKER_COLOR26_GLASS_H == 152U);

    printf("type_needs_wh_swap / uses_ui_page:\n");
    CHECK("color26 swaps w/h", tagtinker_type_needs_wh_swap(TAGTINKER_TYPE_SMARTAG_COLOR_26));
    CHECK("color26 uses ui page", tagtinker_type_uses_ui_page(TAGTINKER_TYPE_SMARTAG_COLOR_26));
    CHECK("type 0 does not swap", !tagtinker_type_needs_wh_swap(0));
    CHECK("other type does not swap", !tagtinker_type_needs_wh_swap(1234));
    CHECK("other type no ui page", !tagtinker_type_uses_ui_page(1234));

    printf("color26_resolve_page (0/1 -> 2, 2..7 kept, >7 clamped):\n");
    CHECK("page 0 -> 2", tagtinker_color26_resolve_page(0) == 2U);
    CHECK("page 1 -> 2", tagtinker_color26_resolve_page(1) == 2U);
    CHECK("page 2 -> 2", tagtinker_color26_resolve_page(2) == 2U);
    CHECK("page 3 kept", tagtinker_color26_resolve_page(3) == 3U);
    CHECK("page 7 kept", tagtinker_color26_resolve_page(7) == 7U);
    CHECK("page 8 clamped to 7", tagtinker_color26_resolve_page(8) == 7U);
    CHECK("page 255 clamped to 7", tagtinker_color26_resolve_page(255) == 7U);

    printf("color26_proto_to_glass (bx=py, by=proto_w-1-px):\n");
    {
        uint16_t bx = 0xFFFF, by = 0xFFFF;
        const uint16_t w = TAGTINKER_COLOR26_WIRE_W; /* 152 */

        tagtinker_color26_proto_to_glass(w, 0, 0, &bx, &by);
        CHECK("(0,0) -> (0,151)", bx == 0U && by == 151U);

        tagtinker_color26_proto_to_glass(w, 0, 295, &bx, &by);
        CHECK("(px=0,py=295) -> (295,151)", bx == 295U && by == 151U);

        tagtinker_color26_proto_to_glass(w, 151, 0, &bx, &by);
        CHECK("(px=151,py=0) -> (0,0)", bx == 0U && by == 0U);

        tagtinker_color26_proto_to_glass(w, 151, 295, &bx, &by);
        CHECK("(px=151,py=295) -> (295,0)", bx == 295U && by == 0U);

        tagtinker_color26_proto_to_glass(w, 10, 20, &bx, &by);
        CHECK("(px=10,py=20) -> (20,141)", bx == 20U && by == 141U);

        /* Every wire coordinate must land inside the glass rectangle. */
        int in_bounds = 1;
        for(uint16_t px = 0; px < TAGTINKER_COLOR26_WIRE_W; px++) {
            for(uint16_t py = 0; py < TAGTINKER_COLOR26_WIRE_H; py++) {
                tagtinker_color26_proto_to_glass(w, px, py, &bx, &by);
                if(bx >= TAGTINKER_COLOR26_GLASS_W || by >= TAGTINKER_COLOR26_GLASS_H) {
                    in_bounds = 0;
                }
            }
        }
        CHECK("whole wire maps inside glass", in_bounds);
    }

    printf("profile helpers (swap + glass size + NULL safety):\n");
    {
        TagTinkerTagProfile color26 = {0};
        color26.type_code = TAGTINKER_TYPE_SMARTAG_COLOR_26;
        color26.width = TAGTINKER_COLOR26_WIRE_W;
        color26.height = TAGTINKER_COLOR26_WIRE_H;

        TagTinkerTagProfile mono = {0};
        mono.type_code = 1234;
        mono.width = 400;
        mono.height = 300;

        CHECK("color26 profile swaps", tagtinker_profile_needs_wh_swap(&color26));
        CHECK("color26 profile uses ui page", tagtinker_profile_uses_ui_page(&color26));
        CHECK("mono profile no swap", !tagtinker_profile_needs_wh_swap(&mono));
        CHECK("NULL profile no swap", !tagtinker_profile_needs_wh_swap(NULL));

        uint16_t w = 0, h = 0;
        tagtinker_profile_glass_size(&color26, &w, &h);
        CHECK("color26 glass size = 296x152", w == 296U && h == 152U);

        w = 0;
        h = 0;
        tagtinker_profile_glass_size(&mono, &w, &h);
        CHECK("mono glass size = profile w/h (400x300)", w == 400U && h == 300U);

        /* NULL arguments must be no-ops, not crashes. */
        tagtinker_profile_glass_size(NULL, &w, &h);
        tagtinker_profile_glass_size(&color26, NULL, &h);
        tagtinker_profile_glass_size(&color26, &w, NULL);
        CHECK("NULL args are no-ops (no crash)", 1);
    }

    printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

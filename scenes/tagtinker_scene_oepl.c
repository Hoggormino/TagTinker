/*
 * OpenEPaperLink status / dry-run preview scene.
 *
 * Read-only: shows the configured AP, the owner's tag allow-list, and a
 * dry-run of the exact POST /imgupload request that the ESP32 bridge would
 * send for the first owned tag. It TRANSMITS NOTHING — live push needs the
 * Phase 2 bridge frame (see docs/oepl-integration-plan.md).
 */
#include "../tagtinker_app.h"
#include "../oepl/tagtinker_oepl.h"
#include <stdarg.h>

/* Bounded appender: keeps the running offset from ever passing the buffer, so
 * later snprintf calls stay in-bounds even after truncation. */
static size_t oepl_append(char* buf, size_t cap, size_t n, const char* fmt, ...) {
    if(n >= cap) return cap - 1;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if(w < 0) return n;
    n += (size_t)w;
    return (n >= cap) ? (cap - 1) : n;
}

void tagtinker_scene_oepl_on_enter(void* ctx) {
    TagTinkerApp* app = ctx;
    static char buf[640];
    size_t n = 0;

    TagTinkerOeplConfig cfg;
    TagTinkerOeplAllowList allow;
    tagtinker_oepl_config_load(&cfg);
    tagtinker_oepl_allowlist_load(&allow);

    n = oepl_append(buf, sizeof(buf), n, "--- OpenEPaperLink ---\n");
    if(cfg.valid) {
        n = oepl_append(buf, sizeof(buf), n, "AP: %s:%u\n", cfg.host, cfg.port);
        n = oepl_append(
            buf, sizeof(buf), n, "Auth: %s\n", cfg.token[0] ? "token set" : "open");
    } else {
        n = oepl_append(buf, sizeof(buf), n, "AP: not configured\n(APP_DATA/oepl.conf)\n");
    }

    n = oepl_append(buf, sizeof(buf), n, "\nOwned tags: %u\n", allow.count);
    for(uint8_t i = 0; i < allow.count && i < 6; i++) {
        n = oepl_append(buf, sizeof(buf), n, "  %s\n", allow.mac[i]);
    }
    if(allow.count == 0) {
        n = oepl_append(buf, sizeof(buf), n, "  (APP_DATA/oepl_tags.txt)\n");
    }

    /* Dry-run preview of the request for the first owned tag. No transmission. */
    if(cfg.valid && allow.count > 0) {
        char hdr[512];
        char trl[64];
        size_t hl = 0, tl = 0;
        uint32_t clen = 0;
        TagTinkerOeplResult r = tagtinker_oepl_build_imgupload_request(
            &cfg, &allow, allow.mac[0], true, 4096u, hdr, sizeof(hdr), &hl, trl,
            sizeof(trl), &tl, &clen);
        if(r == TagTinkerOeplOk) {
            n = oepl_append(
                buf, sizeof(buf), n,
                "\nPreview (no send):\nPOST /imgupload\ntag %s\nbody %lu B\n",
                allow.mac[0], (unsigned long)clen);
        } else {
            n = oepl_append(buf, sizeof(buf), n, "\nPreview: err %d\n", (int)r);
        }
    }

    n = oepl_append(buf, sizeof(buf), n, "\nPush needs the ESP32\nbridge (see docs).");
    (void)n;

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    text_box_set_text(app->text_box, buf);
    view_dispatcher_switch_to_view(app->view_dispatcher, TagTinkerViewTextBox);
}

bool tagtinker_scene_oepl_on_event(void* ctx, SceneManagerEvent event) {
    TagTinkerApp* app = ctx;
    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }
    return false;
}

void tagtinker_scene_oepl_on_exit(void* ctx) {
    TagTinkerApp* app = ctx;
    text_box_reset(app->text_box);
}

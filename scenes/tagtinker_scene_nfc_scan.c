/*
 * NFC Scan scene — scan a Pricer ESL NFC tag to fill the barcode
 *
 * The scanner keeps running after a tag is rejected, so an unsupported tag no
 * longer ends the session: present another one and it is read straight away.
 * Tags are de-duplicated by UID so a tag left resting on the reader does not
 * re-trigger its message every poll.
 */

#include "../tagtinker_app.h"
#include <string.h>

/* Kept above the target-menu submenu indices: that scene treats any custom
 * event below target_count as a target selection, so a late event arriving
 * after Back must not look like one. */
enum {
    NfcScanEventSuccess = 100,
    NfcScanEventNotEsl = 101,
    NfcScanEventVendor = 102,
    NfcScanEventUnreadable = 103,
    NfcScanEventRearm = 104,
};

static void nfc_scan_show_prompt(TagTinkerApp* app) {
    popup_reset(app->popup);
    popup_disable_timeout(app->popup);
    popup_set_callback(app->popup, NULL);
    popup_set_header(app->popup, "Scan NFC Tag", 64, 10, AlignCenter, AlignTop);
    popup_set_text(
        app->popup, "Hold ESL tag\nto Flipper back", 64, 32, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, TagTinkerViewPopup);
}

static void nfc_scan_popup_timeout_cb(void* ctx) {
    TagTinkerApp* app = ctx;
    /* Runs on the popup's timer; hand the work back to the scene's event loop. */
    view_dispatcher_send_custom_event(app->view_dispatcher, NfcScanEventRearm);
}

/* Show a transient message. When rearm is set the scanner is still running, so
 * the prompt comes back by itself; otherwise the message stays until Back. */
static void
    nfc_scan_show_message(TagTinkerApp* app, const char* header, const char* text, bool rearm) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 38, AlignCenter, AlignCenter);
    if(rearm) {
        popup_set_context(app->popup, app);
        popup_set_callback(app->popup, nfc_scan_popup_timeout_cb);
        popup_set_timeout(app->popup, 2500);
        popup_enable_timeout(app->popup);
    } else {
        popup_set_callback(app->popup, NULL);
        popup_disable_timeout(app->popup);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, TagTinkerViewPopup);
}

static int32_t nfc_scan_thread(void* ctx) {
    TagTinkerApp* app = ctx;
    uint8_t last_uid[10];
    uint8_t last_uid_len = 0;

    while(app->nfc_scanning) {
        MfUltralightData* mfu_data = mf_ultralight_alloc();
        MfUltralightError err =
            mf_ultralight_poller_sync_read_card(app->nfc, mfu_data, NULL);

        if(!app->nfc_scanning) {
            mf_ultralight_free(mfu_data);
            break;
        }

        if(err != MfUltralightErrorNone) {
            /* Nothing in the field: forget the last tag so it can be re-read. */
            last_uid_len = 0;
            mf_ultralight_free(mfu_data);
            furi_delay_ms(100);
            continue;
        }

        uint8_t uid_len = mfu_data->iso14443_3a_data->uid_len;
        if(uid_len > sizeof(last_uid)) uid_len = sizeof(last_uid);
        const uint8_t* uid = mfu_data->iso14443_3a_data->uid;

        if(uid_len > 0 && uid_len == last_uid_len && memcmp(uid, last_uid, uid_len) == 0) {
            /* Same tag still resting on the reader; do not announce it again. */
            mf_ultralight_free(mfu_data);
            furi_delay_ms(100);
            continue;
        }
        memcpy(last_uid, uid, uid_len);
        last_uid_len = uid_len;

        char url[TAGTINKER_NFC_URL_LEN];
        char barcode[18];
        bool truncated = false;
        bool have_url = tagtinker_nfc_extract_url(mfu_data, url, sizeof(url), &truncated);

        /* A truncated URL has lost the tail the Pricer id lives in, but it
         * still carries its host, so a vendor can be named either way. */
        const TagTinkerNfcVendorEntry* vendor =
            have_url ? tagtinker_nfc_identify_vendor(url) : NULL;
        uint32_t event;

        if(mfu_data->pages_read == 0) {
            /* Chip answered but no page was readable (not an Ultralight/NTAG). */
            event = NfcScanEventUnreadable;
        } else if(have_url && !truncated && tagtinker_nfc_decode_url(url, barcode)) {
            memcpy(app->barcode, barcode, TAGTINKER_BC_LEN);
            app->barcode[TAGTINKER_BC_LEN] = '\0';
            event = NfcScanEventSuccess;
        } else if(vendor) {
            /* The entry points into a static const table, so publishing it for
             * the scene thread needs no copy and no lifetime handling. */
            app->nfc_vendor = vendor;
            event = NfcScanEventVendor;
        } else {
            event = NfcScanEventNotEsl;
        }

        mf_ultralight_free(mfu_data);

        if(!app->nfc_scanning) break;
        view_dispatcher_send_custom_event(app->view_dispatcher, event);

        /* On success the scene moves on; stop polling so the field is not left
         * running underneath the next scene. */
        if(event == NfcScanEventSuccess) return 0;

        furi_delay_ms(100);
    }

    return 0;
}

void tagtinker_scene_nfc_scan_on_enter(void* ctx) {
    TagTinkerApp* app = ctx;

    nfc_scan_show_prompt(app);

    notification_message(app->notifications, &sequence_blink_start_cyan);

    app->nfc = nfc_alloc();
    app->nfc_vendor = NULL;
    app->nfc_scanning = true;

    furi_thread_set_callback(app->nfc_thread, nfc_scan_thread);
    furi_thread_set_context(app->nfc_thread, app);
    furi_thread_start(app->nfc_thread);
}

bool tagtinker_scene_nfc_scan_on_event(void* ctx, SceneManagerEvent event) {
    TagTinkerApp* app = ctx;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == NfcScanEventSuccess) {
        int8_t idx = tagtinker_ensure_target(app, app->barcode);

        if(idx < 0) {
            /* The barcode came out of the decoder, so it always parses; a
             * failure here means there is no free target slot left. */
            nfc_scan_show_message(
                app, "Target list full", "Delete a target\nand scan again", false);
            return true;
        }

        tagtinker_select_target(app, (uint8_t)idx);

        FURI_LOG_I(
            TAGTINKER_TAG,
            "NFC: %s -> PLID %02X%02X%02X%02X",
            app->barcode,
            app->plid[3],
            app->plid[2],
            app->plid[1],
            app->plid[0]);

        notification_message(app->notifications, &sequence_success);
        scene_manager_next_scene(app->scene_manager, TagTinkerSceneTargetActions);
        return true;
    }

    if(event.event == NfcScanEventVendor) {
        /* A recognised tag from another ESL vendor. Naming it is all this app
         * can do: none of them has an IR receiver, so none can be targeted. */
        const TagTinkerNfcVendorEntry* vendor = app->nfc_vendor;
        if(vendor) {
            nfc_scan_show_message(app, vendor->name, vendor->reason, true);
        } else {
            nfc_scan_show_message(app, "Other ESL tag", "Not a Pricer tag", true);
        }
        return true;
    }

    if(event.event == NfcScanEventNotEsl) {
        nfc_scan_show_message(app, "Not a Pricer tag", "No Pricer ESL\ndata on tag", true);
        return true;
    }

    if(event.event == NfcScanEventUnreadable) {
        nfc_scan_show_message(app, "Unsupported chip", "Could not read\nthis NFC chip", true);
        return true;
    }

    if(event.event == NfcScanEventRearm) {
        nfc_scan_show_prompt(app);
        return true;
    }

    return false;
}

void tagtinker_scene_nfc_scan_on_exit(void* ctx) {
    TagTinkerApp* app = ctx;

    app->nfc_scanning = false;

    if(app->nfc) {
        furi_thread_join(app->nfc_thread);
        nfc_free(app->nfc);
        app->nfc = NULL;
    }

    notification_message(app->notifications, &sequence_blink_stop);
    popup_reset(app->popup);
    popup_disable_timeout(app->popup);
    popup_set_callback(app->popup, NULL);
}

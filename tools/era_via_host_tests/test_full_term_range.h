/* Executes the production VIA handlers and EEPROM load/flush helpers. */
static void test_full_term_range(void) {
    const uint16_t terms[] = {1, 99, 137, 500, 501, 1000, 32767, 32768, 65534, 65535};
    uint8_t report[32];
    for (unsigned i = 0; i < sizeof(terms) / sizeof(terms[0]); i++) {
        const uint16_t term = terms[i];
        expect_true("global exact boundary accepted", tapping_exact_set(term));
        expect_eq_u16("global exact boundary preserved", tapping_exact_get(), term);
        uint32_t revision = era_state_sync_config_revision();
        uint8_t projected = term < 100 ? 10 : term >= 500 ? 50 : (uint8_t)((100 + ((term - 100) / 20) * 20) / 10);
        expect_eq_u8("global legacy projection", tapping_legacy_get(), projected);
        expect_eq_u16("global legacy GET never overwrites exact", tapping_exact_get(), term);
        expect_true("global projection does not publish mutation", era_state_sync_config_revision() == revision);
        expect_true("global same-value SET accepted", tapping_exact_set(term));
        expect_true("global same-value SET does not publish mutation", era_state_sync_config_revision() == revision);
        for (uint8_t slot = 0; slot < 8; slot++) {
            expect_true("TD exact boundary accepted", tapdance_exact_set(slot, term));
            expect_eq_u16("TD exact boundary preserved", tapdance_exact_get(slot), term);
            expect_eq_u16("TD runtime consumes exact term", tapdance_get_term_ms(QK_TAP_DANCE | slot), term);
            revision = era_state_sync_config_revision();
            zero_report(report);
            report[0] = id_custom_get_value; report[1] = id_qmk_tapdance; report[2] = (slot + 1) * 5;
            expect_true("TD legacy GET handled", tapdance_handle_via_command(report, 32));
            expect_eq_u8("TD legacy projection", report[3], projected);
            expect_eq_u16("TD projection preserves exact", tapdance_exact_get(slot), term);
            expect_true("TD same-value SET accepted", tapdance_exact_set(slot, term));
            expect_true("TD projection/no-op preserve revision", era_state_sync_config_revision() == revision);
        }
    }
    /* Exhaustively test the BE16 domain, without producing 589815 log lines. */
    bool all_exact = true;
    for (uint32_t value = 1; value <= UINT16_MAX; value++) {
        all_exact &= tapping_exact_set((uint16_t)value) && tapping_exact_get() == value;
        for (uint8_t slot = 0; slot < 8; slot++) {
            all_exact &= tapdance_exact_set(slot, (uint16_t)value) && tapdance_exact_get(slot) == value;
        }
    }
    expect_true("every nonzero uint16 round-trips global and all eight slots", all_exact);

    uint32_t revision = era_state_sync_config_revision();
    expect_true("global zero rejected", !tapping_exact_set(0));
    expect_true("TD zero rejected", !tapdance_exact_set(7, 0));
    for (uint8_t command = id_custom_set_value; command <= id_custom_get_value; command++) {
        for (uint8_t length = 0; length < 5; length++) {
            memset(report, 0xa5, sizeof(report));
            report[0] = command; report[1] = id_qmk_tapping; report[2] = id_qmk_tapping_global_term_exact;
            expect_true("global incomplete exact frame rejected", !tapping_term_handle_via_command(report, length));
            expect_eq_u8("global short frame payload untouched", report[4], 0xa5);
            report[0] = command; report[1] = id_qmk_tapdance; report[2] = id_qmk_tapdance_8_term_exact;
            expect_true("TD incomplete exact frame rejected", !tapdance_handle_via_command(report, length));
            expect_eq_u8("TD short frame payload untouched", report[4], 0xa5);
        }
    }
    expect_eq_u16("invalid global preserves store", tapping_exact_get(), UINT16_MAX);
    expect_eq_u16("invalid TD preserves store", tapdance_exact_get(7), UINT16_MAX);
    expect_true("invalid commands do not publish mutation", era_state_sync_config_revision() == revision);

    for (uint8_t slot = 0; slot < 8; slot++) tapdance_exact_set(slot, terms[slot]);
    zero_report(report); report[0] = id_custom_save;
    report[1] = id_qmk_tapping;
    expect_true("global SAVE accepted", tapping_term_handle_via_command(report, 32));
    report[1] = id_qmk_tapdance;
    expect_true("TD SAVE accepted", tapdance_handle_via_command(report, 32));
    tapping_exact_set(333);
    for (uint8_t slot = 0; slot < 8; slot++) tapdance_exact_set(slot, 444);
    tapping_term_init(); tapdance_init();
    expect_eq_u16("global saved maximum survives reinit", tapping_exact_get(), UINT16_MAX);
    for (uint8_t slot = 0; slot < 8; slot++) {
        expect_eq_u16("independent exact slot survives reinit", tapdance_exact_get(slot), terms[slot]);
    }
    /* A legacy SET changes only its selected value, using the existing grid. */
    zero_report(report); report[0] = id_custom_set_value; report[1] = id_qmk_tapdance;
    report[2] = 5; report[3] = 51;
    expect_true("legacy TD SET accepted", tapdance_handle_via_command(report, 32));
    expect_eq_u16("legacy TD SET normalizes to 500", tapdance_exact_get(0), 500);
    expect_eq_u16("legacy TD SET preserves another exact slot", tapdance_exact_get(1), 99);
    expect_eq_u16("legacy TD SET preserves global", tapping_exact_get(), UINT16_MAX);
}

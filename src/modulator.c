#include "quiet/common.h"

modulator *create_modulator(const modulator_options *opt) {
    if (!opt) {
        return NULL;
    }

    modulator *m = malloc(sizeof(modulator));

    m->opt = *opt;

    m->nco = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_phase(m->nco, 0.0f);
    nco_crcf_set_frequency(m->nco, opt->center_rads);

    if (opt->samples_per_symbol > 1) {
        m->interp = firinterp_crcf_create_kaiser(opt->samples_per_symbol,
                                                 opt->symbol_delay, 60.0f);
    } else {
        m->opt.samples_per_symbol = 1;
        m->opt.symbol_delay = 0;
        m->interp = NULL;
    }

    if (opt->premix_filter_opt.order) {
        filter_options filteropt = opt->premix_filter_opt;
        m->premixfilter =
            iirfilt_crcf_create_lowpass(filteropt.order, filteropt.cutoff);
    } else {
        m->premixfilter = NULL;
    }
    m->mixfilter = NULL;
    if (opt->dc_filter_opt.alpha) {
        m->dcfilter = iirfilt_crcf_create_dc_blocker(opt->dc_filter_opt.alpha);
    } else {
        m->dcfilter = NULL;
    }

    return m;
}

size_t modulate_sample_len(const modulator *m, size_t symbol_len) {
    if (!m) {
        return 0;
    }

    return m->opt.samples_per_symbol * symbol_len;
}

size_t modulate_symbol_len(const modulator *m, size_t sample_len) {
    if (!m) {
        return 0;
    }

    return sample_len / (m->opt.samples_per_symbol);
}

// modulate assumes that samples is large enough to store symbol_len *
// samples_per_symbol samples
// returns number of samples written to *samples
size_t modulate(modulator *m, const float complex *symbols, size_t symbol_len,
                sample_t *samples) {
    if (!m) {
        return 0;
    }

    float complex post_filter[m->opt.samples_per_symbol];
    size_t written = 0;
    for (size_t i = 0; i < symbol_len; i++) {
        if (m->interp) {
            firinterp_crcf_execute(m->interp, symbols[i], &post_filter[0]);
        } else {
            post_filter[0] = symbols[i];  // pass thru
        }
        for (size_t j = 0; j < m->opt.samples_per_symbol; j++) {
            float complex mixed;
            if (m->premixfilter) {
                iirfilt_crcf_execute(m->premixfilter, post_filter[j],
                                     &post_filter[j]);
            }
            nco_crcf_mix_up(m->nco, post_filter[j], &mixed);
            if (m->dcfilter) {
                iirfilt_crcf_execute(m->dcfilter, mixed, &mixed);
            }
            samples[i * m->opt.samples_per_symbol + j] =
                crealf(mixed) * m->opt.gain;
            written++;
            nco_crcf_step(m->nco);
        }
    }
    return written;
}

size_t modulate_flush_sample_len(modulator *m) {
    if (!m) {
        return 0;
    }

    return m->opt.samples_per_symbol * 2 * m->opt.symbol_delay;
}

size_t modulate_flush(modulator *m, sample_t *samples) {
    if (!m) {
        return 0;
    }

    if (!m->opt.symbol_delay) {
        return 0;
    }

    size_t symbol_len = 2 * m->opt.symbol_delay;
    float complex terminate[symbol_len];
    for (size_t i = 0; i < symbol_len; i++) {
        terminate[i] = 0;
    }

    return modulate(m, terminate, symbol_len, samples);
}

void modulate_reset(modulator *m) {
    firinterp_crcf_reset(m->interp);
    if (m->dcfilter) {
        iirfilt_crcf_reset(m->dcfilter);
    }
}

void destroy_modulator(modulator *m) {
    if (!m) {
        return;
    }

    nco_crcf_destroy(m->nco);
    if (m->interp) {
        firinterp_crcf_destroy(m->interp);
    }
    if (m->premixfilter) {
        iirfilt_crcf_destroy(m->premixfilter);
    }
    if (m->mixfilter) {
        firfilt_crcf_destroy(m->mixfilter);
    }
    if (m->dcfilter) {
        iirfilt_crcf_destroy(m->dcfilter);
    }
    free(m);
}

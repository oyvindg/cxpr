/* Minimal offline-vert: klokke -> generert cxpr -> PCM16 stereo WAV.
 * Sang, instrumenter, miks, effekter, gain og fade ligger i cxpr-modellene.
 */
#include <cxpr/generated.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "song.gen.c"

enum { DEFAULT_SAMPLE_RATE = 44100, MIN_SAMPLE_RATE = 8000, MAX_SAMPLE_RATE = 44100 };

static size_t find_name(const char *const *names, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i)
        if (strcmp(names[i], name) == 0) return i;
    return SIZE_MAX;
}

static void little16(unsigned char *bytes, uint16_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

static void little32(unsigned char *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (8 * i));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Bruk: render-native OUTPUT.wav [--frames=COUNT] [--sample-rate=44100]\n");
        return 2;
    }
    uint32_t sample_rate = DEFAULT_SAMPLE_RATE;
    uint32_t requested_frames = 0;
    for (int argument = 2; argument < argc; ++argument) {
        const char *frame_prefix = "--frames=";
        const char *rate_prefix = "--sample-rate=";
        const char *value = NULL;
        char *end = NULL;
        unsigned long parsed;
        if (strncmp(argv[argument], frame_prefix, strlen(frame_prefix)) == 0)
            value = argv[argument] + strlen(frame_prefix);
        else if (strncmp(argv[argument], rate_prefix, strlen(rate_prefix)) == 0)
            value = argv[argument] + strlen(rate_prefix);
        else {
            fprintf(stderr, "Ukjent valg: %s\n", argv[argument]); return 2;
        }
        parsed = strtoul(value, &end, 10);
        if (!end || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
            fprintf(stderr, "Ugyldig heltallsverdi: %s\n", value); return 2;
        }
        if (strncmp(argv[argument], frame_prefix, strlen(frame_prefix)) == 0)
            requested_frames = (uint32_t)parsed;
        else
            sample_rate = (uint32_t)parsed;
    }
    if (sample_rate < MIN_SAMPLE_RATE || sample_rate > MAX_SAMPLE_RATE) {
        fprintf(stderr, "Sample rate må være mellom %u og %u Hz.\n",
                MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
        return 2;
    }
    const cxpr_generated_model_descriptor *model = &cxpr_model_tick_descriptor;
    if (!cxpr_generated_model_descriptor_abi_valid(model)) {
        fprintf(stderr, "Ugyldig generert cxpr-ABI.\n"); return 1;
    }
    size_t time_slot = find_name(model->input_names, model->input_count, "t");
    size_t left_slot = find_name(model->output_names, model->output_count, "left");
    size_t right_slot = find_name(model->output_names, model->output_count, "right");
    size_t duration_slot = find_name(model->output_names, model->output_count, "duration");
    if (model->input_count != 1 || time_slot == SIZE_MAX ||
        model->input_types[time_slot] != CXPR_GENERATED_VALUE_NUMBER ||
        left_slot == SIZE_MAX || right_slot == SIZE_MAX || duration_slot == SIZE_MAX) {
        fprintf(stderr, "Modellen må ha input t og outputs left, right, duration.\n"); return 1;
    }
    cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {0};
    cxpr_value params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {0};
    cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {0};
    size_t sample_rate_slot = SIZE_MAX;
    for (size_t i = 0; i < model->param_count; ++i) {
        if (!model->param_has_default[i]) {
            fprintf(stderr, "Parameter uten default: %s\n", model->param_names[i]); return 1;
        }
        params[i] = model->param_defaults[i];
        if (strcmp(model->param_names[i], "sample_rate") == 0) {
            sample_rate_slot = i;
            params[i] = cxpr_num(sample_rate);
        }
    }
    if (sample_rate_slot == SIZE_MAX) {
        fprintf(stderr, "Modellen må eksponere parameteren $sample_rate.\n"); return 1;
    }
    size_t state_size = model->state_size();
    void *state = calloc(1, state_size ? state_size : 1);
    if (!state) { fprintf(stderr, "Kunne ikke allokere modellstate.\n"); return 1; }
    if (model->reset) model->reset(state);
    inputs[time_slot] = cxpr_num(0);
    model->tick(state, inputs, params, outputs);
    double duration = outputs[duration_slot].type == CXPR_VALUE_NUMBER
        ? outputs[duration_slot].d : NAN;
    if (!isfinite(duration) || duration <= 0 ||
        duration * sample_rate > (UINT32_MAX - 36u) / 4u) {
        fprintf(stderr, "Ugyldig varighet for PCM16 RIFF/WAV.\n"); free(state); return 1;
    }
    uint32_t frames = (uint32_t)llround(duration * sample_rate);
    if (requested_frames && requested_frames < frames) frames = requested_frames;
    unsigned char header[44] = {0};
    memcpy(header, "RIFF", 4); little32(header + 4, 36u + frames * 4u);
    memcpy(header + 8, "WAVEfmt ", 8); little32(header + 16, 16);
    little16(header + 20, 1); little16(header + 22, 2);
    little32(header + 24, sample_rate); little32(header + 28, sample_rate * 4);
    little16(header + 32, 4); little16(header + 34, 16);
    memcpy(header + 36, "data", 4); little32(header + 40, frames * 4u);
    FILE *file = fopen(argv[1], "wb");
    if (!file) { perror(argv[1]); free(state); return 1; }
    int result = 1;
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) goto cleanup;
    unsigned char block[4096 * 4];
    size_t used = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        if (frame != 0) {
            inputs[time_slot] = cxpr_num((double)frame / sample_rate);
            model->tick(state, inputs, params, outputs);
        }
        const size_t channels[2] = {left_slot, right_slot};
        for (unsigned channel = 0; channel < 2; ++channel) {
            cxpr_value sample = outputs[channels[channel]];
            if (sample.type != CXPR_VALUE_NUMBER || !isfinite(sample.d) || fabs(sample.d) > 1) {
                fprintf(stderr, "Ugyldig sample ved ramme %u, kanal %u.\n", frame, channel);
                goto cleanup;
            }
            int16_t pcm = (int16_t)lround(sample.d * 32767);
            little16(block + used, (uint16_t)pcm);
            used += 2;
        }
        if (used == sizeof(block)) {
            if (fwrite(block, 1, used, file) != used) goto cleanup;
            used = 0;
        }
    }
    if (used && fwrite(block, 1, used, file) != used) goto cleanup;
    fprintf(stderr, "%s: %u frames, %.6f s, %zu bytes state, stereo %u Hz.\n",
        model->name, frames, (double)frames / sample_rate, state_size, sample_rate);
    result = 0;
cleanup:
    if (fclose(file) != 0) result = 1;
    free(state);
    if (result != 0) { remove(argv[1]); fprintf(stderr, "WAV-renderingen mislyktes.\n"); }
    return result;
}

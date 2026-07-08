#include <cxpr/cxpr.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef CXPR_BENCH_RSI_MODEL_INLINE
#include CXPR_BENCH_RSI_MODEL_INLINE
#endif

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static volatile double g_sink = 0.0;

void cxpr_bench_rsi_state_tick_c(double* slots,
                                 const double* inputs,
                                 const double* params,
                                 double* outputs);

static char* read_fixture(const char* relative_path) {
    char path[1024];
    FILE* f;
    long size;
    char* data;

    snprintf(path, sizeof(path), "%s/%s", CXPR_TEST_SOURCE_DIR, relative_path);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open fixture: %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = (char*)malloc((size_t)size + 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';
    fclose(f);
    return data;
}

static long long now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static double host_above(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    if (argc != 2u) return 0.0;
    return args[0] > args[1] ? 1.0 : 0.0;
}

static double host_score(const double* args, size_t argc, void* userdata) {
    const double value = argc > 0u ? args[0] : 0.0;
    const double from = argc > 1u ? args[1] : 0.0;
    const double to = argc > 2u ? args[2] : 0.0;
    (void)userdata;
    if (argc < 3u || from == to) return 0.0;
    if (from < to) {
        if (value <= from) return 0.0;
        if (value >= to) return 1.0;
        return (value - from) / (to - from);
    }
    if (value >= from) return 0.0;
    if (value <= to) return 1.0;
    return (from - value) / (from - to);
}

static double host_cross_above4(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    if (argc != 4u) return 0.0;
    return args[2] <= args[3] && args[0] > args[1] ? 1.0 : 0.0;
}

static double host_cross_below4(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    if (argc != 4u) return 0.0;
    return args[2] >= args[3] && args[0] < args[1] ? 1.0 : 0.0;
}

static void register_host_cxta_signals(cxpr_registry* reg) {
    cxpr_registry_add_numeric(reg, "above", host_above, 2, 2, NULL, NULL);
    cxpr_registry_add_numeric(reg, "score", host_score, 3, 3, NULL, NULL);
    cxpr_registry_add_numeric(reg, "cross_above4", host_cross_above4, 4, 4, NULL, NULL);
    cxpr_registry_add_numeric(reg, "cross_below4", host_cross_below4, 4, 4, NULL, NULL);
}

static const char* cxta_signal_model_source(void) {
    return
        "name cxta_signal_bench\n"
        "in { value, from, to, cur_left, cur_right, prev_left, prev_right }\n"
        "fn above(left, right) = left > right\n"
        "fn score(value, from, to) =\n"
        "    if(from == to,\n"
        "       0,\n"
        "       if(from < to,\n"
        "          if(value <= from, 0, if(value >= to, 1, (value - from) / (to - from))),\n"
        "          if(value >= from, 0, if(value <= to, 1, (from - value) / (from - to)))))\n"
        "fn cross_above4(cur_left, cur_right, prev_left, prev_right) =\n"
        "    prev_left <= prev_right and cur_left > cur_right\n"
        "fn cross_below4(cur_left, cur_right, prev_left, prev_right) =\n"
        "    prev_left >= prev_right and cur_left < cur_right\n"
        "above_signal = above(cur_left, cur_right)\n"
        "score_value = score(value, from, to)\n"
        "cross_up = cross_above4(cur_left, cur_right, prev_left, prev_right)\n"
        "cross_down = cross_below4(cur_left, cur_right, prev_left, prev_right)\n"
        "signal = above_signal and score_value >= 0.5 and (cross_up or not cross_down)\n"
        "out signal\n";
}

#ifdef CXPR_BENCH_INCLUDE_COLD_PATH
static double time_host_registered_setup(size_t iterations) {
    long long start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        cxpr_error err = {0};
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_evaluator* eval = NULL;
        if (!reg) abort();
        cxpr_registry_add_numeric(reg, "above", host_above, 2, 2, NULL, NULL);
        eval = cxpr_evaluator_new(reg);
        if (!eval) abort();
        if (!cxpr_expression_add(eval, "signal", "above(close, 10)", &err)) {
            fprintf(stderr, "host setup add failed: %s\n", err.message);
            abort();
        }
        if (!cxpr_evaluator_compile(eval, &err)) {
            fprintf(stderr, "host setup compile failed: %s\n", err.message);
            abort();
        }
        cxpr_evaluator_free(eval);
        cxpr_registry_free(reg);
    }
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_model_setup(size_t iterations) {
    static const char* source =
        "name bench\n"
        "in { close }\n"
        "fn above(src, threshold) = src > threshold\n"
        "signal = above(close, 10)\n"
        "out signal\n";
    long long start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        cxpr_error err = {0};
        cxpr_model* model = cxpr_parse_model(source, &err);
        cxpr_model_program* program = NULL;
        if (!model) {
            fprintf(stderr, "model parse failed: %s\n", err.message);
            abort();
        }
        program = cxpr_compile_model(model, NULL, &err);
        if (!program) {
            fprintf(stderr, "model compile failed: %s\n", err.message);
            abort();
        }
        cxpr_model_program_free(program);
        cxpr_model_free(model);
    }
    return (double)(now_ns() - start) / (double)iterations;
}
#endif

static double time_host_registered_eval(size_t iterations) {
    cxpr_error err = {0};
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* eval;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!reg) abort();
    cxpr_registry_add_numeric(reg, "above", host_above, 2, 2, NULL, NULL);
    eval = cxpr_evaluator_new(reg);
    ctx = cxpr_context_new();
    if (!eval || !ctx) abort();
    if (!cxpr_expression_add(eval, "signal", "above(close, 10)", &err) ||
        !cxpr_evaluator_compile(eval, &err)) {
        fprintf(stderr, "host eval setup failed: %s\n", err.message);
        abort();
    }

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool found = false;
        cxpr_context_set(ctx, "close", 9.0 + (double)(i & 3u));
        cxpr_evaluator_eval(eval, ctx, &err);
        if (err.code != CXPR_OK) {
            fprintf(stderr, "host eval failed: %s\n", err.message);
            abort();
        }
        total += cxpr_expression_get_double(eval, "signal", &found);
        if (!found) abort();
    }

    cxpr_context_free(ctx);
    cxpr_evaluator_free(eval);
    cxpr_registry_free(reg);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_model_eval(size_t iterations) {
    static const char* source =
        "name bench\n"
        "in { close }\n"
        "fn above(src, threshold) = src > threshold\n"
        "signal = above(close, 10)\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    cxpr_model_program* program;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!model) {
        fprintf(stderr, "model parse failed: %s\n", err.message);
        abort();
    }
    program = cxpr_compile_model(model, NULL, &err);
    ctx = cxpr_context_new();
    if (!program || !ctx) {
        fprintf(stderr, "model eval setup failed: %s\n", err.message);
        abort();
    }

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool found = false;
        cxpr_context_set(ctx, "close", 9.0 + (double)(i & 3u));
        if (!cxpr_eval_model_program(program, ctx, NULL, &err)) {
            fprintf(stderr, "model eval failed: %s\n", err.message);
            abort();
        }
        total += cxpr_context_get_bool(ctx, "signal", &found) ? 1.0 : 0.0;
        if (!found) abort();
    }

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_host_registered_cxta_signal_eval(size_t iterations) {
    cxpr_error err = {0};
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* eval;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!reg) abort();
    register_host_cxta_signals(reg);
    eval = cxpr_evaluator_new(reg);
    ctx = cxpr_context_new();
    if (!eval || !ctx) abort();
    if (!cxpr_expression_add(eval, "above_signal", "above(cur_left, cur_right)", &err) ||
        !cxpr_expression_add(eval, "score_value", "score(value, from, to)", &err) ||
        !cxpr_expression_add(eval, "cross_up", "cross_above4(cur_left, cur_right, prev_left, prev_right)", &err) ||
        !cxpr_expression_add(eval, "cross_down", "cross_below4(cur_left, cur_right, prev_left, prev_right)", &err) ||
        !cxpr_expression_add(eval, "signal", "above_signal > 0 and score_value >= 0.5 and (cross_up > 0 or not (cross_down > 0))", &err) ||
        !cxpr_evaluator_compile(eval, &err)) {
        fprintf(stderr, "host cxta signal eval setup failed: %s\n", err.message);
        abort();
    }

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool found = false;
        cxpr_context_set(ctx, "value", (double)(i % 11u));
        cxpr_context_set(ctx, "from", (i & 1u) ? 10.0 : 0.0);
        cxpr_context_set(ctx, "to", (i & 1u) ? 0.0 : 10.0);
        cxpr_context_set(ctx, "cur_left", (i & 1u) ? 1.0 : 3.0);
        cxpr_context_set(ctx, "cur_right", 2.0);
        cxpr_context_set(ctx, "prev_left", (i & 1u) ? 3.0 : 1.0);
        cxpr_context_set(ctx, "prev_right", 2.0);
        cxpr_evaluator_eval(eval, ctx, &err);
        if (err.code != CXPR_OK) {
            fprintf(stderr, "host cxta signal eval failed: %s\n", err.message);
            abort();
        }
        total += cxpr_expression_get_bool(eval, "signal", &found) ? 1.0 : 0.0;
        if (!found) abort();
    }

    cxpr_context_free(ctx);
    cxpr_evaluator_free(eval);
    cxpr_registry_free(reg);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_model_cxta_signal_eval(size_t iterations) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(cxta_signal_model_source(), &err);
    cxpr_model_program* program;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!model) {
        fprintf(stderr, "model cxta signal parse failed: %s\n", err.message);
        abort();
    }
    program = cxpr_compile_model(model, NULL, &err);
    ctx = cxpr_context_new();
    if (!program || !ctx) {
        fprintf(stderr, "model cxta signal setup failed: %s\n", err.message);
        abort();
    }

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool found = false;
        cxpr_context_set(ctx, "value", (double)(i % 11u));
        cxpr_context_set(ctx, "from", (i & 1u) ? 10.0 : 0.0);
        cxpr_context_set(ctx, "to", (i & 1u) ? 0.0 : 10.0);
        cxpr_context_set(ctx, "cur_left", (i & 1u) ? 1.0 : 3.0);
        cxpr_context_set(ctx, "cur_right", 2.0);
        cxpr_context_set(ctx, "prev_left", (i & 1u) ? 3.0 : 1.0);
        cxpr_context_set(ctx, "prev_right", 2.0);
        if (!cxpr_eval_model_program(program, ctx, NULL, &err)) {
            fprintf(stderr, "model cxta signal eval failed: %s\n", err.message);
            abort();
        }
        total += cxpr_context_get_bool(ctx, "signal", &found) ? 1.0 : 0.0;
        if (!found) abort();
    }

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_strategy_fixture_eval(size_t iterations) {
    cxpr_error err = {0};
    char* source = read_fixture("fixtures/strategies/ensemble_score_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!source) abort();
    model = cxpr_parse_model(source, &err);
    if (!model) {
        fprintf(stderr, "strategy fixture parse failed: %s\n", err.message);
        abort();
    }
    program = cxpr_compile_model(model, NULL, &err);
    ctx = cxpr_context_new();
    if (!program || !ctx) {
        fprintf(stderr, "strategy fixture setup failed: %s\n", err.message);
        abort();
    }
    if (!cxpr_model_program_seed_defaults(program, ctx, NULL, &err)) {
        fprintf(stderr, "strategy fixture seed failed: %s\n", err.message);
        abort();
    }

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool found = false;
        double close = 98.0 + (double)(i & 7u);
        cxpr_context_set(ctx, "close", close);
        cxpr_context_set(ctx, "ema_f", close + 4.0);
        cxpr_context_set(ctx, "ema_s", close);
        cxpr_context_set(ctx, "macd_histogram", 0.15 + 0.05 * (double)(i & 3u));
        cxpr_context_set(ctx, "rsi_value", 46.0 + (double)(i & 7u));
        cxpr_context_set(ctx, "bb_lower", close - 10.0);
        cxpr_context_set(ctx, "volume", 140.0 + (double)(i & 15u));
        cxpr_context_set(ctx, "vol_ma", 100.0);
        cxpr_context_set(ctx, "atr_value", 3.0 + 0.1 * (double)(i & 3u));
        if (!cxpr_eval_model_program(program, ctx, NULL, &err)) {
            fprintf(stderr, "strategy fixture eval failed: %s\n", err.message);
            abort();
        }
        total += cxpr_context_get_bool(ctx, "entry", &found) ? 1.0 : 0.0;
        if (!found) abort();
        total += cxpr_context_get(ctx, "entry_score", &found);
        if (!found) abort();
    }

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_rsi_state_strategy_fixture_tick(size_t iterations) {
    cxpr_error err = {0};
    char* source = read_fixture("fixtures/strategies/rsi_state_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!source) abort();
    model = cxpr_parse_model(source, &err);
    if (!model) {
        fprintf(stderr, "rsi strategy fixture parse failed: %s\n", err.message);
        abort();
    }
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) {
        fprintf(stderr, "rsi strategy fixture compile failed: %s\n", err.message);
        abort();
    }
    session = cxpr_model_session_new(program, NULL, &err);
    if (!session) {
        fprintf(stderr, "rsi strategy fixture session failed: %s\n", err.message);
        abort();
    }
    ctx = cxpr_model_session_context(session);
    if (!ctx) abort();

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool entry = false;
        double r = 0.0;
        const double wave = (double)(i % 17u);
        cxpr_context_set(ctx, "close", 100.0 + wave);
        cxpr_context_set(ctx, "trend", 99.0 + wave * 0.2);
        if (!cxpr_model_session_tick(program, session, NULL, &err)) {
            fprintf(stderr, "rsi strategy fixture tick failed: %s\n", err.message);
            abort();
        }
        if (!cxpr_model_session_output_bool(session, "entry", &entry)) abort();
        total += entry ? 1.0 : 0.0;
        if (!cxpr_model_session_output_number(session, "r", &r)) abort();
        total += r;
    }

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_rsi_state_strategy_fixture_c_tick(size_t iterations) {
    double slots[128] = {0};
    const double params[] = {3.0, 60.0, 45.0};
    double inputs[2] = {0};
    double outputs[5] = {0};
    long long start;
    double total = 0.0;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        const double wave = (double)(i % 17u);
        inputs[0] = 100.0 + wave;
        inputs[1] = 99.0 + wave * 0.2;
        cxpr_bench_rsi_state_tick_c(slots, inputs, params, outputs);
        total += outputs[0] != 0.0 ? 1.0 : 0.0;
        total += outputs[2];
    }

    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_rsi_state_strategy_fixture_c_inline_tick(size_t iterations) {
    double slots[128] = {0};
    const double params[] = {3.0, 60.0, 45.0};
    double inputs[2] = {0};
    double outputs[5] = {0};
    long long start;
    double total = 0.0;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        const double wave = (double)(i % 17u);
        inputs[0] = 100.0 + wave;
        inputs[1] = 99.0 + wave * 0.2;
        cxpr_bench_rsi_state_tick_inline_c(slots, inputs, params, outputs);
        total += outputs[0] != 0.0 ? 1.0 : 0.0;
        total += outputs[2];
    }

    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static inline void rsi_codegen_const_params_tick(double* restrict _cx_slots,
                                                 const double* restrict _cx_inputs,
                                                 double* restrict _cx_outputs) {
    const double _cx_input_0 = _cx_inputs[0];
    const double _cx_input_1 = _cx_inputs[1];
    const double _cx_param_0 = 3.0;
    const double _cx_param_1 = 60.0;
    const double _cx_param_2 = 45.0;
    const double _cx_state_initialized = _cx_slots[2];
    const double _cx_state_bars = _cx_slots[3];
    const double _cx_state_prev_close = _cx_slots[4];
    const double _cx_state_gain_sum = _cx_slots[5];
    const double _cx_state_loss_sum = _cx_slots[6];
    const double _cx_state_avg_gain = _cx_slots[7];
    const double _cx_state_avg_loss = _cx_slots[8];
    const double change = (((_cx_state_initialized > 0)) ? ((_cx_input_0 - _cx_state_prev_close)) : (0));
    const double next_bars = (_cx_state_bars + 1);
    const double next_delta_count = (((_cx_state_bars > _cx_param_0)) ? (_cx_param_0) : (_cx_state_bars));
    const double seed_tick = (_cx_state_bars == _cx_param_0);
    const double _cx_next_initialized = 1;
    const double _cx_next_prev_close = _cx_input_0;
    const double gain = ((change > 0) ? (change) : (0));
    const double loss = (((0 - change) > 0) ? ((0 - change)) : (0));
    const double _cx_next_bars = next_bars;
    const double warmed = (next_delta_count >= _cx_param_0);
    const double next_gain_sum = (((_cx_state_bars <= _cx_param_0)) ? ((_cx_state_gain_sum + gain)) : (_cx_state_gain_sum));
    const double next_loss_sum = (((_cx_state_bars <= _cx_param_0)) ? ((_cx_state_loss_sum + loss)) : (_cx_state_loss_sum));
    const double next_avg_gain = (((!warmed)) ? (_cx_state_avg_gain) : (((seed_tick) ? ((next_gain_sum / _cx_param_0)) : ((((_cx_state_avg_gain * (_cx_param_0 - 1)) + gain) / _cx_param_0)))));
    const double _cx_next_gain_sum = next_gain_sum;
    const double next_avg_loss = (((!warmed)) ? (_cx_state_avg_loss) : (((seed_tick) ? ((next_loss_sum / _cx_param_0)) : ((((_cx_state_avg_loss * (_cx_param_0 - 1)) + loss) / _cx_param_0)))));
    const double _cx_next_loss_sum = next_loss_sum;
    const double _cx_next_avg_gain = next_avg_gain;
    const double r = ((warmed) ? (((((next_avg_gain == 0) && (next_avg_loss == 0))) ? (50) : ((((next_avg_loss == 0)) ? (100) : ((100 - (100 / (1 + (next_avg_gain / next_avg_loss))))))))) : (50));
    const double _cx_next_avg_loss = next_avg_loss;
    const double entry = ((r > _cx_param_1) && (_cx_input_0 > _cx_input_1));
    const double exit = ((r < _cx_param_2) || (_cx_input_0 < _cx_input_1));
    _cx_slots[2] = _cx_next_initialized;
    _cx_slots[3] = _cx_next_bars;
    _cx_slots[4] = _cx_next_prev_close;
    _cx_slots[5] = _cx_next_gain_sum;
    _cx_slots[6] = _cx_next_loss_sum;
    _cx_slots[7] = _cx_next_avg_gain;
    _cx_slots[8] = _cx_next_avg_loss;
    _cx_outputs[0] = entry;
    _cx_outputs[1] = exit;
    _cx_outputs[2] = r;
    _cx_outputs[3] = _cx_next_avg_gain;
    _cx_outputs[4] = _cx_next_avg_loss;
}

static double time_rsi_state_strategy_codegen_const_params_tick(size_t iterations) {
    double slots[128] = {0};
    double inputs[2] = {0};
    double outputs[5] = {0};
    long long start;
    double total = 0.0;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        const double wave = (double)(i % 17u);
        inputs[0] = 100.0 + wave;
        inputs[1] = 99.0 + wave * 0.2;
        rsi_codegen_const_params_tick(slots, inputs, outputs);
        total += outputs[0] != 0.0 ? 1.0 : 0.0;
        total += outputs[2];
    }

    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

typedef struct {
    double initialized;
    double bars;
    double prev_close;
    double gain_sum;
    double loss_sum;
    double avg_gain;
    double avg_loss;
} rsi_lowlevel_state;

static inline double rsi_lowlevel_rsi(double avg_gain, double avg_loss) {
    if (avg_gain == 0.0 && avg_loss == 0.0) return 50.0;
    if (avg_loss == 0.0) return 100.0;
    return 100.0 - 100.0 / (1.0 + avg_gain / avg_loss);
}

static inline void rsi_lowlevel_tick(rsi_lowlevel_state* s,
                                     double close,
                                     double trend,
                                     double period,
                                     double entry_rsi,
                                     double exit_rsi,
                                     double* outputs) {
    const double change = s->initialized > 0.0 ? close - s->prev_close : 0.0;
    const double gain = change > 0.0 ? change : 0.0;
    const double loss = (0.0 - change) > 0.0 ? (0.0 - change) : 0.0;
    const double next_bars = s->bars + 1.0;
    const double next_delta_count = s->bars > period ? period : s->bars;
    const double next_gain_sum = s->bars <= period ? s->gain_sum + gain : s->gain_sum;
    const double next_loss_sum = s->bars <= period ? s->loss_sum + loss : s->loss_sum;
    const bool warmed = next_delta_count >= period;
    const bool seed_tick = s->bars == period;
    const double next_avg_gain =
        !warmed ? s->avg_gain :
        seed_tick ? next_gain_sum / period :
        (s->avg_gain * (period - 1.0) + gain) / period;
    const double next_avg_loss =
        !warmed ? s->avg_loss :
        seed_tick ? next_loss_sum / period :
        (s->avg_loss * (period - 1.0) + loss) / period;
    const double r = warmed ? rsi_lowlevel_rsi(next_avg_gain, next_avg_loss) : 50.0;
    const bool entry = r > entry_rsi && close > trend;
    const bool exit = r < exit_rsi || close < trend;

    s->initialized = 1.0;
    s->bars = next_bars;
    s->prev_close = close;
    s->gain_sum = next_gain_sum;
    s->loss_sum = next_loss_sum;
    s->avg_gain = next_avg_gain;
    s->avg_loss = next_avg_loss;

    outputs[0] = entry ? 1.0 : 0.0;
    outputs[1] = exit ? 1.0 : 0.0;
    outputs[2] = r;
    outputs[3] = s->avg_gain;
    outputs[4] = s->avg_loss;
}

static inline void rsi_lowlevel_slot_tick(double* restrict slots,
                                          const double* restrict inputs,
                                          const double* restrict params,
                                          double* restrict outputs) {
    const double close = inputs[0];
    const double trend = inputs[1];
    const double period = params[0];
    const double entry_rsi = params[1];
    const double exit_rsi = params[2];
    const double initialized = slots[2];
    const double bars = slots[3];
    const double prev_close = slots[4];
    const double gain_sum = slots[5];
    const double loss_sum = slots[6];
    const double avg_gain = slots[7];
    const double avg_loss = slots[8];
    const double change = initialized > 0.0 ? close - prev_close : 0.0;
    const double gain = change > 0.0 ? change : 0.0;
    const double loss = (0.0 - change) > 0.0 ? (0.0 - change) : 0.0;
    const double next_bars = bars + 1.0;
    const double next_delta_count = bars > period ? period : bars;
    const double next_gain_sum = bars <= period ? gain_sum + gain : gain_sum;
    const double next_loss_sum = bars <= period ? loss_sum + loss : loss_sum;
    const bool warmed = next_delta_count >= period;
    const bool seed_tick = bars == period;
    const double next_avg_gain =
        !warmed ? avg_gain :
        seed_tick ? next_gain_sum / period :
        (avg_gain * (period - 1.0) + gain) / period;
    const double next_avg_loss =
        !warmed ? avg_loss :
        seed_tick ? next_loss_sum / period :
        (avg_loss * (period - 1.0) + loss) / period;
    const double r = warmed ? rsi_lowlevel_rsi(next_avg_gain, next_avg_loss) : 50.0;
    const bool entry = r > entry_rsi && close > trend;
    const bool exit = r < exit_rsi || close < trend;

    slots[2] = 1.0;
    slots[3] = next_bars;
    slots[4] = close;
    slots[5] = next_gain_sum;
    slots[6] = next_loss_sum;
    slots[7] = next_avg_gain;
    slots[8] = next_avg_loss;

    outputs[0] = entry ? 1.0 : 0.0;
    outputs[1] = exit ? 1.0 : 0.0;
    outputs[2] = r;
    outputs[3] = next_avg_gain;
    outputs[4] = next_avg_loss;
}

static double time_rsi_state_strategy_lowlevel_slot_tick(size_t iterations) {
    double slots[128] = {0};
    const double params[] = {3.0, 60.0, 45.0};
    double inputs[2] = {0};
    double outputs[5] = {0};
    long long start;
    double total = 0.0;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        const double wave = (double)(i % 17u);
        inputs[0] = 100.0 + wave;
        inputs[1] = 99.0 + wave * 0.2;
        rsi_lowlevel_slot_tick(slots, inputs, params, outputs);
        total += outputs[0] != 0.0 ? 1.0 : 0.0;
        total += outputs[2];
    }

    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_rsi_state_strategy_lowlevel_tick(size_t iterations) {
    rsi_lowlevel_state state = {0};
    double outputs[5] = {0};
    long long start;
    double total = 0.0;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        const double wave = (double)(i % 17u);
        rsi_lowlevel_tick(&state,
                          100.0 + wave,
                          99.0 + wave * 0.2,
                          3.0,
                          60.0,
                          45.0,
                          outputs);
        total += outputs[0] != 0.0 ? 1.0 : 0.0;
        total += outputs[2];
    }

    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_macd_record_strategy_fixture_tick(size_t iterations) {
    cxpr_error err = {0};
    char* source = read_fixture("fixtures/strategies/macd_record_cross_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;
    long long start;
    double total = 0.0;

    if (!source) abort();
    model = cxpr_parse_model(source, &err);
    if (!model) {
        fprintf(stderr, "macd record fixture parse failed: %s\n", err.message);
        abort();
    }
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) {
        fprintf(stderr, "macd record fixture compile failed: %s\n", err.message);
        abort();
    }
    session = cxpr_model_session_new(program, NULL, &err);
    if (!session) {
        fprintf(stderr, "macd record fixture session failed: %s\n", err.message);
        abort();
    }
    ctx = cxpr_model_session_context(session);
    if (!ctx) abort();

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        bool entry = false;
        const double close = (i & 1u) ? 11.0 : 9.0;
        cxpr_context_set(ctx, "close", close);
        if (!cxpr_model_session_tick(program, session, NULL, &err)) {
            fprintf(stderr, "macd record fixture tick failed: %s\n", err.message);
            abort();
        }
        if (!cxpr_model_session_output_bool(session, "entry", &entry)) abort();
        total += entry ? 1.0 : 0.0;
    }

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    g_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static size_t model_registry_function_count(void) {
    static const char* source =
        "name bench\n"
        "in { close }\n"
        "fn above(src, threshold) = src > threshold\n"
        "signal = above(close, 10)\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    cxpr_model_program* program;
    size_t count;
    if (!model) abort();
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) abort();
    count = cxpr_model_program_function_count(program);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    return count;
}

static size_t rsi_fused_ir_instruction_count(void) {
    cxpr_error err = {0};
    char* source = read_fixture("fixtures/strategies/rsi_state_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    size_t count;
    if (!source) abort();
    model = cxpr_parse_model(source, &err);
    if (!model) abort();
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) abort();
    count = cxpr_model_program_fused_ir_instruction_count(program);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    return count;
}

static const char* rsi_fused_ir_disabled_opcode(void) {
    cxpr_error err = {0};
    char* source = read_fixture("fixtures/strategies/rsi_state_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    const char* opcode;
    if (!source) abort();
    model = cxpr_parse_model(source, &err);
    if (!model) abort();
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) abort();
    opcode = cxpr_model_program_fused_ir_disabled_opcode(program);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    return opcode;
}

int main(void) {
    const size_t eval_iters = 200000u;
    const double host_eval = time_host_registered_eval(eval_iters);
    const double model_eval = time_model_eval(eval_iters);
    const double host_cxta_signal_eval = time_host_registered_cxta_signal_eval(eval_iters);
    const double model_cxta_signal_eval = time_model_cxta_signal_eval(eval_iters);
    const double strategy_fixture_eval = time_strategy_fixture_eval(eval_iters);
    const double rsi_state_fixture_tick = time_rsi_state_strategy_fixture_tick(eval_iters);
    const double rsi_state_fixture_c_tick = time_rsi_state_strategy_fixture_c_tick(eval_iters);
    const double rsi_state_fixture_c_inline_tick =
        time_rsi_state_strategy_fixture_c_inline_tick(eval_iters);
    const double rsi_state_fixture_codegen_const_params_tick =
        time_rsi_state_strategy_codegen_const_params_tick(eval_iters);
    const double rsi_state_fixture_lowlevel_slot_tick =
        time_rsi_state_strategy_lowlevel_slot_tick(eval_iters);
    const double rsi_state_fixture_lowlevel_tick =
        time_rsi_state_strategy_lowlevel_tick(eval_iters);
    const double macd_record_fixture_tick = time_macd_record_strategy_fixture_tick(eval_iters);

    printf("cxpr model runtime benchmark\n");
    printf("note: parse, compile, session creation and generated-C build are outside timed loops\n");
    printf("eval  host_registered_fn: %.2f ns/op\n", host_eval);
    printf("eval  cxpr_model_fn:      %.2f ns/op (%.2fx host)\n",
           model_eval, model_eval / host_eval);
    printf("eval  host_cxta_signals:  %.2f ns/op\n", host_cxta_signal_eval);
    printf("eval  cxpr_cxta_signals:  %.2f ns/op (%.2fx host)\n",
           model_cxta_signal_eval, model_cxta_signal_eval / host_cxta_signal_eval);
    printf("eval  cxpr_strategy_fixture: %.2f ns/op\n", strategy_fixture_eval);
    printf("tick  cxpr_rsi_state_fixture: %.2f ns/op\n", rsi_state_fixture_tick);
    printf("tick  cxpr_rsi_state_c:       %.2f ns/op (%.2fx fused IR)\n",
           rsi_state_fixture_c_tick, rsi_state_fixture_c_tick / rsi_state_fixture_tick);
    printf("tick  cxpr_rsi_state_c_inline: %.2f ns/op (%.2fx lowlevel)\n",
           rsi_state_fixture_c_inline_tick,
           rsi_state_fixture_c_inline_tick / rsi_state_fixture_lowlevel_tick);
    printf("tick  cxpr_rsi_state_c_const_params: %.2f ns/op (%.2fx cxpr C)\n",
           rsi_state_fixture_codegen_const_params_tick,
           rsi_state_fixture_codegen_const_params_tick / rsi_state_fixture_c_tick);
    printf("tick  lowlevel_rsi_state_slot_c: %.2f ns/op (%.2fx lowlevel)\n",
           rsi_state_fixture_lowlevel_slot_tick,
           rsi_state_fixture_lowlevel_slot_tick / rsi_state_fixture_lowlevel_tick);
    printf("tick  lowlevel_rsi_state_c:   %.2f ns/op (cxpr C %.2fx lowlevel)\n",
           rsi_state_fixture_lowlevel_tick,
           rsi_state_fixture_c_tick / rsi_state_fixture_lowlevel_tick);
    printf("tick  cxpr_macd_record_fixture: %.2f ns/op\n", macd_record_fixture_tick);
    printf("model registry functions: %zu\n", model_registry_function_count());
    printf("rsi fused IR instructions: %zu\n", rsi_fused_ir_instruction_count());
    printf("rsi fused disabled opcode: %s\n",
           rsi_fused_ir_disabled_opcode() ? rsi_fused_ir_disabled_opcode() : "(none)");
    printf("sink: %.0f\n", g_sink);
    return 0;
}

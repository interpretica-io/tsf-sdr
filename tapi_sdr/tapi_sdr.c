/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Software defined radio from a test
 */

#define TE_LGR_USER "TAPI SDR"

#include "te_config.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_str.h"
#include "te_string.h"
#include "te_vector.h"

#include "tapi_sdr.h"
#include "tapi_sdr_internal.h"

/** The sample rate used when nothing says otherwise. */
#define SDR_DEFAULT_RATE 2048000

/** How long a recording lasts when nothing says otherwise, s. */
#define SDR_DEFAULT_SECONDS 5

/** Arguments of a command, as a plain vector of strings. */
typedef struct sdr_cmd_opt {
    /** Number of arguments. */
    size_t n_args;
    /** Arguments after argv[0]. */
    const char **args;
} sdr_cmd_opt;

static const tapi_job_opt_bind sdr_cmd_binds[] = TAPI_JOB_OPT_SET(
    TAPI_JOB_OPT_ARRAY_PTR(sdr_cmd_opt, n_args, args,
        TAPI_JOB_OPT_CONTENT(TAPI_JOB_OPT_STRING, NULL, false))
);

const tapi_sdr_capture_opt tapi_sdr_capture_default_opt = {
    .backend = TAPI_SDR_AUTO,
    .sample_rate_hz = SDR_DEFAULT_RATE,
    .seconds = SDR_DEFAULT_SECONDS,
};

/* See description in tapi_sdr.h */
const char *
tapi_sdr_backend2str(tapi_sdr_backend backend)
{
    switch (backend)
    {
        case TAPI_SDR_RTL:
            return "rtl-sdr";
        case TAPI_SDR_HACKRF:
            return "hackrf";
        case TAPI_SDR_SOAPY:
            return "soapysdr";
        default:
            return "auto";
    }
}

/* See description in tapi_sdr.h */
unsigned int
tapi_sdr_features(tapi_sdr_backend backend)
{
    switch (backend)
    {
        case TAPI_SDR_RTL:
            /*
             * A television tuner with the wrong driver on it. It hears
             * everything and says nothing, and no amount of software
             * will change that - there is no transmitter in it.
             */
            return TAPI_SDR_FEAT_RECEIVE;

        case TAPI_SDR_HACKRF:
            return TAPI_SDR_FEAT_RECEIVE | TAPI_SDR_FEAT_TRANSMIT;

        case TAPI_SDR_SOAPY:
            /*
             * Soapy is a library over other radios, so what it can do
             * is whatever is plugged in. Receive is claimed because
             * every device it supports can; transmit is not, because
             * claiming it would be a promise about hardware this
             * library has not seen.
             */
            return TAPI_SDR_FEAT_RECEIVE;

        default:
            return 0;
    }
}

/* See description in tapi_sdr.h */
bool
tapi_sdr_supports(tapi_sdr_backend backend, unsigned int feature)
{
    return (tapi_sdr_features(backend) & feature) == feature;
}

/* See description in tapi_sdr_internal.h */
void
tapi_sdr_arg(te_vec *args, const char *fmt, ...)
{
    te_string built = TE_STRING_INIT;
    char *arg;
    va_list ap;

    va_start(ap, fmt);
    te_string_append_va(&built, fmt, ap);
    va_end(ap);

    arg = built.ptr;
    TE_VEC_APPEND(args, arg);
}

/* See description in tapi_sdr_internal.h */
te_errno
tapi_sdr_cmd(tapi_job_factory_t *factory, const char *program,
             const te_vec *args, int timeout_ms, te_string *out,
             te_string *err, int *exit_code)
{
    sdr_cmd_opt opt = {
        .n_args = te_vec_size(args),
        .args = te_vec_size(args) == 0 ? NULL :
                (const char **)te_vec_get((te_vec *)args, 0),
    };
    tapi_devtool_output output;
    tapi_devtool_run run = TAPI_DEVTOOL_RUN_INIT;
    te_errno rc;

    rc = tapi_devtool_run_init(&run, factory, "sdr", program, sdr_cmd_binds,
                               &opt, NULL);
    if (rc != 0)
        return rc;

    rc = tapi_devtool_run_start(&run);
    if (rc == 0)
        rc = tapi_devtool_run_wait(&run, timeout_ms);

    if (rc != 0)
    {
        tapi_devtool_run_fini(&run);
        return rc;
    }

    tapi_devtool_run_get_output(&run, &output);

    if (out != NULL && output.out != NULL)
        te_string_append(out, "%s", output.out);
    if (err != NULL && output.err != NULL)
        te_string_append(err, "%s", output.err);

    if (exit_code != NULL)
    {
        *exit_code = output.status.type == TAPI_JOB_STATUS_EXITED ?
                     output.status.value : -1;
    }

    return tapi_devtool_run_fini(&run);
}

/** Run one tool with a fixed argument list. */
static te_errno
sdr_run(tapi_job_factory_t *factory, const char *program,
        const char *const *argv, size_t argc, int timeout_ms,
        te_string *out, int *exit_code)
{
    te_vec args = TE_VEC_INIT(char *);
    size_t i;
    te_errno rc;

    for (i = 0; i < argc; i++)
        tapi_sdr_arg(&args, "%s", argv[i]);

    rc = tapi_sdr_cmd(factory, program, &args, timeout_ms, out, out,
                      exit_code);

    te_vec_deep_free(&args);

    return rc;
}

/** Is there an RTL-SDR dongle? */
static bool
sdr_probe_rtl(tapi_job_factory_t *factory, int timeout_ms)
{
    static const char *const argv[] = { "-t" };
    te_string out = TE_STRING_INIT;
    int code = 0;
    bool found;

    /*
     * Measured: with nothing plugged in, rtl_test prints "No supported
     * devices found." and exits 1. It is one of the honest ones, so
     * the status can be trusted - but the text is checked as well,
     * because a tool that is not installed also exits non-zero and
     * that is a different answer.
     */
    if (sdr_run(factory, "rtl_test", argv, TE_ARRAY_LEN(argv), timeout_ms,
                &out, &code) != 0)
    {
        te_string_free(&out);
        return false;
    }

    found = code == 0 &&
            strstr(te_string_value(&out), "No supported devices") == NULL;

    te_string_free(&out);

    return found;
}

/** Is there a HackRF? */
static bool
sdr_probe_hackrf(tapi_job_factory_t *factory, int timeout_ms)
{
    te_string out = TE_STRING_INIT;
    int code = 0;
    bool found;

    /*
     * hackrf_info prints its own version banner before finding out
     * whether anything is attached, so the banner proves only that the
     * program exists. The serial number is what proves a radio does.
     */
    if (sdr_run(factory, "hackrf_info", NULL, 0, timeout_ms, &out,
                &code) != 0)
    {
        te_string_free(&out);
        return false;
    }

    found = code == 0 &&
            strstr(te_string_value(&out), "Serial number") != NULL;

    te_string_free(&out);

    return found;
}

/** Is there anything SoapySDR knows about? */
static bool
sdr_probe_soapy(tapi_job_factory_t *factory, int timeout_ms)
{
    static const char *const argv[] = { "--find" };
    te_string out = TE_STRING_INIT;
    int code = 0;
    bool found;

    /*
     * --find, never --info. Measured: SoapySDRUtil --info exits 0 with
     * nothing attached at all, because it is reporting on its own
     * modules rather than on any hardware. Using it would make every
     * agent look as though it had a radio.
     */
    if (sdr_run(factory, "SoapySDRUtil", argv, TE_ARRAY_LEN(argv),
                timeout_ms, &out, &code) != 0)
    {
        te_string_free(&out);
        return false;
    }

    found = code == 0 &&
            strstr(te_string_value(&out), "Found device") != NULL;

    te_string_free(&out);

    return found;
}

/* See description in tapi_sdr.h */
te_errno
tapi_sdr_detect(tapi_job_factory_t *factory, int timeout_ms,
                tapi_sdr_backend *backend)
{
    /*
     * HackRF first, because it is the only one that can transmit and a
     * rig that has one wants it used. Then rtl-sdr, which is the
     * common one. Soapy last: it can see the other two, so asking it
     * first would hide which radio is really there.
     */
    if (sdr_probe_hackrf(factory, timeout_ms))
        *backend = TAPI_SDR_HACKRF;
    else if (sdr_probe_rtl(factory, timeout_ms))
        *backend = TAPI_SDR_RTL;
    else if (sdr_probe_soapy(factory, timeout_ms))
        *backend = TAPI_SDR_SOAPY;
    else
        return TE_RC(TE_TAPI, TE_ENOENT);

    RING("The agent has a radio through %s",
         tapi_sdr_backend2str(*backend));

    return 0;
}

/* See description in tapi_sdr.h */
bool
tapi_sdr_available(tapi_job_factory_t *factory, tapi_sdr_backend backend,
                   int timeout_ms)
{
    switch (backend)
    {
        case TAPI_SDR_AUTO:
        {
            tapi_sdr_backend found;

            return tapi_sdr_detect(factory, timeout_ms, &found) == 0;
        }

        case TAPI_SDR_RTL:
            return sdr_probe_rtl(factory, timeout_ms);
        case TAPI_SDR_HACKRF:
            return sdr_probe_hackrf(factory, timeout_ms);
        case TAPI_SDR_SOAPY:
            return sdr_probe_soapy(factory, timeout_ms);
    }

    return false;
}

/** Resolve TAPI_SDR_AUTO. */
static te_errno
sdr_backend(tapi_job_factory_t *factory, tapi_sdr_backend requested,
            int timeout_ms, tapi_sdr_backend *backend)
{
    if (requested != TAPI_SDR_AUTO)
    {
        *backend = requested;
        return 0;
    }

    return tapi_sdr_detect(factory, timeout_ms, backend);
}

/* See description in tapi_sdr.h */
te_errno
tapi_sdr_capture(tapi_job_factory_t *factory,
                 const tapi_sdr_capture_opt *opt, const char *path,
                 int timeout_ms)
{
    tapi_sdr_backend backend;
    te_vec args = TE_VEC_INIT(char *);
    te_string out = TE_STRING_INIT;
    uint32_t rate = opt->sample_rate_hz != 0 ? opt->sample_rate_hz :
                    SDR_DEFAULT_RATE;
    unsigned int seconds = opt->seconds != 0 ? opt->seconds :
                           SDR_DEFAULT_SECONDS;
    const char *program;
    int code = 0;
    size_t i;
    te_errno rc;

    if (opt->frequency_hz == 0)
    {
        ERROR("A capture needs a frequency");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    rc = sdr_backend(factory, opt->backend, timeout_ms, &backend);
    if (rc != 0)
        return TE_RC(TE_TAPI, TE_ENODEV);

    switch (backend)
    {
        case TAPI_SDR_HACKRF:
            program = "hackrf_transfer";
            tapi_sdr_arg(&args, "-r");
            tapi_sdr_arg(&args, "%s", path);
            tapi_sdr_arg(&args, "-f");
            tapi_sdr_arg(&args, "%" PRIu64, opt->frequency_hz);
            tapi_sdr_arg(&args, "-s");
            tapi_sdr_arg(&args, "%u", rate);
            if (opt->gain_db != 0)
            {
                tapi_sdr_arg(&args, "-l");
                tapi_sdr_arg(&args, "%u", opt->gain_db);
            }
            /* It counts samples, not seconds. */
            tapi_sdr_arg(&args, "-n");
            tapi_sdr_arg(&args, "%" PRIu64,
                         opt->samples != 0 ? (uint64_t)opt->samples :
                             (uint64_t)rate * seconds);
            break;

        default:
            program = "rtl_sdr";
            tapi_sdr_arg(&args, "-f");
            tapi_sdr_arg(&args, "%" PRIu64, opt->frequency_hz);
            tapi_sdr_arg(&args, "-s");
            tapi_sdr_arg(&args, "%u", rate);
            if (opt->gain_db != 0)
            {
                tapi_sdr_arg(&args, "-g");
                tapi_sdr_arg(&args, "%u", opt->gain_db);
            }
            if (opt->device_index != 0)
            {
                tapi_sdr_arg(&args, "-d");
                tapi_sdr_arg(&args, "%u", opt->device_index);
            }
            tapi_sdr_arg(&args, "-n");
            tapi_sdr_arg(&args, "%" PRIu64,
                         opt->samples != 0 ? (uint64_t)opt->samples :
                             (uint64_t)rate * seconds);
            /* The file comes last and without a flag. */
            tapi_sdr_arg(&args, "%s", path);
            break;
    }

    for (i = 0; i < opt->n_extra_args; i++)
        tapi_sdr_arg(&args, "%s", opt->extra_args[i]);

    RING("Recording %u s at %" PRIu64 " Hz through %s into %s", seconds,
         opt->frequency_hz, tapi_sdr_backend2str(backend), path);

    rc = tapi_sdr_cmd(factory, program, &args, timeout_ms, &out, &out,
                      &code);

    if (rc == 0 && code != 0)
    {
        ERROR("Recording failed (status %d): %s", code,
              te_string_value(&out));
        rc = TE_RC(TE_TAPI, TE_EFAIL);
    }

    te_vec_deep_free(&args);
    te_string_free(&out);

    return rc;
}

/* See description in tapi_sdr.h */
te_errno
tapi_sdr_transmit(tapi_job_factory_t *factory,
                  const tapi_sdr_capture_opt *opt, const char *path,
                  int timeout_ms)
{
    tapi_sdr_backend backend;
    te_vec args = TE_VEC_INIT(char *);
    te_string out = TE_STRING_INIT;
    uint32_t rate = opt->sample_rate_hz != 0 ? opt->sample_rate_hz :
                    SDR_DEFAULT_RATE;
    int code = 0;
    te_errno rc;

    rc = sdr_backend(factory, opt->backend, timeout_ms, &backend);
    if (rc != 0)
        return TE_RC(TE_TAPI, TE_ENODEV);

    if (!tapi_sdr_supports(backend, TAPI_SDR_FEAT_TRANSMIT))
    {
        ERROR("%s cannot transmit. An RTL-SDR dongle is a television "
              "tuner with a different driver on it: there is no "
              "transmitter in it, and no software will add one. A replay "
              "test needs a HackRF or something like it.",
              tapi_sdr_backend2str(backend));
        return TE_RC(TE_TAPI, TE_EOPNOTSUPP);
    }

    tapi_sdr_arg(&args, "-t");
    tapi_sdr_arg(&args, "%s", path);
    tapi_sdr_arg(&args, "-f");
    tapi_sdr_arg(&args, "%" PRIu64, opt->frequency_hz);
    tapi_sdr_arg(&args, "-s");
    tapi_sdr_arg(&args, "%u", rate);
    if (opt->gain_db != 0)
    {
        tapi_sdr_arg(&args, "-x");
        tapi_sdr_arg(&args, "%u", opt->gain_db);
    }

    WARN("Transmitting %s at %" PRIu64 " Hz. This puts energy on the air; "
         "the rig is responsible for where it goes.", path,
         opt->frequency_hz);

    rc = tapi_sdr_cmd(factory, "hackrf_transfer", &args, timeout_ms, &out,
                      &out, &code);

    if (rc == 0 && code != 0)
    {
        ERROR("Transmitting failed (status %d): %s", code,
              te_string_value(&out));
        rc = TE_RC(TE_TAPI, TE_EFAIL);
    }

    te_vec_deep_free(&args);
    te_string_free(&out);

    return rc;
}

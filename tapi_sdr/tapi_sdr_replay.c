/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Record it, send it again
 */

#define TE_LGR_USER "TAPI SDR"

#include "te_config.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_vector.h"

#include "tapi_sdr_replay.h"
#include "tapi_sdr_internal.h"

/** The rate these devices are usually looked at with. */
#define SDR_REPLAY_RATE 250000

/** How long a recording lasts when nothing says otherwise, s. */
#define SDR_REPLAY_SECONDS 5

const tapi_sdr_replay_opt tapi_sdr_replay_default_opt = {
    .backend = TAPI_SDR_AUTO,
    .sample_rate_hz = SDR_REPLAY_RATE,
    .record_seconds = SDR_REPLAY_SECONDS,
};

/** Turn replay options into capture options. */
static tapi_sdr_capture_opt
sdr_capture_of(const tapi_sdr_replay_opt *opt)
{
    tapi_sdr_capture_opt capture = tapi_sdr_capture_default_opt;

    capture.backend = opt->backend;
    capture.frequency_hz = opt->frequency_hz;
    capture.sample_rate_hz = opt->sample_rate_hz != 0 ?
                             opt->sample_rate_hz : SDR_REPLAY_RATE;
    capture.seconds = opt->record_seconds != 0 ?
                      opt->record_seconds : SDR_REPLAY_SECONDS;
    capture.gain_db = opt->gain_db;

    return capture;
}

/* See description in tapi_sdr_replay.h */
te_errno
tapi_sdr_replay_record(tapi_job_factory_t *factory,
                       const tapi_sdr_replay_opt *opt, const char *path,
                       int timeout_ms)
{
    tapi_sdr_capture_opt capture = sdr_capture_of(opt);

    return tapi_sdr_capture(factory, &capture, path, timeout_ms);
}

/* See description in tapi_sdr_replay.h */
te_errno
tapi_sdr_replay_send(tapi_job_factory_t *factory,
                     const tapi_sdr_replay_opt *opt, const char *path,
                     int timeout_ms)
{
    tapi_sdr_capture_opt capture = sdr_capture_of(opt);

    return tapi_sdr_transmit(factory, &capture, path, timeout_ms);
}

/** Pull one string field out of a flat JSON object. */
static char *
sdr_json_str(const char *object, const char *key)
{
    te_string quoted = TE_STRING_INIT;
    const char *found;
    const char *end;
    char *value;

    te_string_append(&quoted, "\"%s\"", key);
    found = strstr(object, quoted.ptr);
    te_string_free(&quoted);

    if (found == NULL)
        return NULL;

    found = strchr(found, ':');
    if (found == NULL)
        return NULL;

    found++;
    while (*found == ' ')
        found++;

    /*
     * A value may be a string or a number, and both are wanted: an
     * identifier comes back as a number from some decoders and as a
     * string from others, and a caller comparing them should not have
     * to know which.
     */
    if (*found == '"')
    {
        found++;
        end = strchr(found, '"');
    }
    else
    {
        end = found + strcspn(found, ",}");
    }

    if (end == NULL)
        return NULL;

    value = TE_ALLOC((size_t)(end - found) + 1);
    memcpy(value, found, (size_t)(end - found));

    return value;
}

/* See description in tapi_sdr_replay.h */
te_errno
tapi_sdr_decode(tapi_job_factory_t *factory,
                const tapi_sdr_replay_opt *opt, const char *path,
                int timeout_ms, te_vec *events)
{
    te_vec args = TE_VEC_INIT(char *);
    te_string out = TE_STRING_INIT;
    const char *line;
    int code = 0;
    te_errno rc;

    *events = (te_vec)TE_VEC_INIT(tapi_sdr_event);

    tapi_sdr_arg(&args, "-r");
    tapi_sdr_arg(&args, "%s", path);
    tapi_sdr_arg(&args, "-s");
    tapi_sdr_arg(&args, "%u", opt->sample_rate_hz != 0 ?
                              opt->sample_rate_hz : SDR_REPLAY_RATE);
    tapi_sdr_arg(&args, "-F");
    tapi_sdr_arg(&args, "json");

    rc = tapi_sdr_cmd(factory, "rtl_433", &args, timeout_ms, &out, NULL,
                      &code);
    te_vec_deep_free(&args);

    if (rc != 0)
    {
        te_string_free(&out);
        return rc;
    }

    /*
     * Measured on rtl_433 23.11: a file it cannot decode anything from
     * is read successfully and produces no output at all - exit 0 and
     * silence. So silence is an answer, not a failure, and the caller
     * is told how many events there were rather than being given an
     * error. A file that does not exist is different: it says
     * "Opening file ... failed!".
     */
    if (code != 0)
    {
        ERROR("The decoder could not read %s (status %d)", path, code);
        te_string_free(&out);
        return TE_RC(TE_TAPI, TE_ENOENT);
    }

    for (line = te_string_value(&out); line != NULL && *line != '\0'; )
    {
        const char *end = strchr(line, '\n');
        size_t len = end != NULL ? (size_t)(end - line) : strlen(line);

        if (len > 2 && line[0] == '{')
        {
            tapi_sdr_event event;
            char *object = TE_ALLOC(len + 1);

            memcpy(object, line, len);

            memset(&event, 0, sizeof(event));
            event.model = sdr_json_str(object, "model");
            event.id = sdr_json_str(object, "id");
            event.raw = object;

            TE_VEC_APPEND(events, event);
        }

        line = end != NULL ? end + 1 : NULL;
    }

    RING("The decoder found %zu things in %s", te_vec_size(events), path);

    te_string_free(&out);

    return 0;
}

/* See description in tapi_sdr_replay.h */
te_errno
tapi_sdr_replay_compare(tapi_job_factory_t *factory,
                        const tapi_sdr_replay_opt *opt, const char *first,
                        const char *second, int timeout_ms,
                        tapi_cybersec_report *report)
{
    te_vec a;
    te_vec b;
    te_string subject = TE_STRING_INIT;
    size_t i;
    size_t j;
    bool repeated = false;
    te_errno rc;

    te_string_append(&subject, "%" PRIu64 " Hz", opt->frequency_hz);

    rc = tapi_sdr_decode(factory, opt, first, timeout_ms, &a);
    if (rc != 0)
        goto out;

    rc = tapi_sdr_decode(factory, opt, second, timeout_ms, &b);
    if (rc != 0)
    {
        tapi_sdr_events_free(&a);
        goto out;
    }

    tapi_sdr_events_log("the first recording", &a);
    tapi_sdr_events_log("the second recording", &b);

    if (te_vec_size(&a) == 0 || te_vec_size(&b) == 0)
    {
        /*
         * Said plainly rather than left as an absence of findings. The
         * decoder knows several hundred devices and not this one, or
         * the recordings caught nothing; either way nothing about the
         * device was established, and a silent report would read as a
         * device that is fine.
         */
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_INFO,
            "sdr.nothing-decoded", te_string_value(&subject),
            "The decoder recognised nothing in one or both recordings, "
            "so whether this device repeats itself was not established. "
            "It knows several hundred devices and may not know this one.");
        goto free_out;
    }

    for (i = 0; i < te_vec_size(&a) && !repeated; i++)
    {
        const tapi_sdr_event *left = te_vec_get(&a, i);

        for (j = 0; j < te_vec_size(&b); j++)
        {
            const tapi_sdr_event *right = te_vec_get(&b, j);

            if (left->model == NULL || right->model == NULL)
                continue;

            if (strcmp(left->model, right->model) != 0)
                continue;

            if (left->id == NULL || right->id == NULL)
                continue;

            if (strcmp(left->id, right->id) != 0)
                continue;

            /*
             * The same device sending the same identifier twice is not
             * itself the finding - a rolling code device keeps its
             * identifier and changes the code. What settles it is the
             * whole record being identical.
             */
            if (left->raw != NULL && right->raw != NULL &&
                strcmp(left->raw, right->raw) == 0)
            {
                repeated = true;

                tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_HIGH,
                    "sdr.repeats-itself", left->model,
                    "%s sent an identical transmission twice at %s. "
                    "There is nothing in it that changes, so anything "
                    "that recorded it once can impersonate it from then "
                    "on.", left->model, te_string_value(&subject));
                break;
            }
        }
    }

    if (!repeated)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_INFO,
            "sdr.differs-between-sends", te_string_value(&subject),
            "The two recordings decoded differently, which is what a "
            "device with a rolling code looks like. That a recording "
            "cannot be replayed was not demonstrated, only that the "
            "obvious way does not work.");
    }

free_out:
    tapi_sdr_events_free(&a);
    tapi_sdr_events_free(&b);

out:
    te_string_free(&subject);

    return rc;
}

/* See description in tapi_sdr_replay.h */
void
tapi_sdr_events_log(const char *what, const te_vec *events)
{
    size_t i;

    RING("%zu things in %s", te_vec_size(events), what);

    for (i = 0; i < te_vec_size(events); i++)
    {
        const tapi_sdr_event *event = te_vec_get((te_vec *)events, i);

        RING("  %s id %s", event->model != NULL ? event->model : "?",
             event->id != NULL ? event->id : "?");
    }
}

/* See description in tapi_sdr_replay.h */
void
tapi_sdr_events_free(te_vec *events)
{
    size_t i;

    for (i = 0; i < te_vec_size(events); i++)
    {
        tapi_sdr_event *event = te_vec_get(events, i);

        free(event->model);
        free(event->id);
        free(event->raw);
    }

    te_vec_free(events);
}

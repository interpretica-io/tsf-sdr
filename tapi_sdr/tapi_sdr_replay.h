/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Record it, send it again
 *
 * @defgroup tapi_sdr_replay Replay and decode (tapi_sdr_replay)
 * @ingroup tapi_sdr
 * @{
 *
 * The test that finds something on a device with no protocol worth the
 * name: **record a transmission and send it again**. A device that
 * acts on the recording has no replay protection at all, and an
 * astonishing number of them do not - a garage door, a doorbell, a
 * gate remote, a cheap sensor.
 *
 * A device that is safe from this sends something different every
 * time, usually a rolling code. A device that is not sends the same
 * few hundred microseconds for its whole life, and anyone who recorded
 * it once can open the door for ever.
 *
 * @code
 * tapi_sdr_replay_opt opt = tapi_sdr_replay_default_opt;
 *
 * opt.frequency_hz = 433920000;
 * opt.record_seconds = 5;
 *
 * CHECK_RC(tapi_sdr_replay_record(factory, &opt, "/tmp/a.cu8", 30000));
 * ... press the remote again ...
 * CHECK_RC(tapi_sdr_replay_record(factory, &opt, "/tmp/b.cu8", 30000));
 * tapi_sdr_replay_compare(factory, &opt, "/tmp/a.cu8", "/tmp/b.cu8",
 *                         30000, &report);
 * @endcode
 *
 * @section tapi_sdr_replay_two Two ways to find it, and one is safe
 *
 * **Comparing two recordings** needs no transmitter and puts nothing
 * on the air. Press the remote twice, record both, and see whether the
 * decoder reads the same code out of each. Two identical codes is the
 * finding. This works with a television-tuner dongle and is what most
 * rigs can do.
 *
 * **Sending the recording back** is the demonstration, and it needs a
 * radio that can transmit and a great deal more care. It is a separate
 * function for that reason.
 */

#ifndef __TSF_TAPI_SDR_REPLAY_H__
#define __TSF_TAPI_SDR_REPLAY_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "te_vector.h"

#include "tapi_cybersec.h"
#include "tapi_sdr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One thing a decoder recognised. */
typedef struct tapi_sdr_event {
    /** What the decoder called the device model. */
    char *model;
    /** Its identifier, if it gave one. */
    char *id;
    /** The whole record, as the decoder printed it. */
    char *raw;
} tapi_sdr_event;

/** How to record and compare. */
typedef struct tapi_sdr_replay_opt {
    /** Which radio; @ref TAPI_SDR_AUTO to ask the agent. */
    tapi_sdr_backend backend;
    /** Centre frequency in hertz. Required. */
    uint64_t frequency_hz;
    /** Sample rate; @c 0 means 250 000, which suits these devices. */
    uint32_t sample_rate_hz;
    /** How long each recording lasts. */
    unsigned int record_seconds;
    /** Gain in decibels; @c 0 to let the radio decide. */
    unsigned int gain_db;
} tapi_sdr_replay_opt;

/** Defaults: 250 kHz, five seconds. */
extern const tapi_sdr_replay_opt tapi_sdr_replay_default_opt;

/**
 * Record a window into a file.
 *
 * @param factory       Job factory.
 * @param opt           Frequency and duration.
 * @param path          Where to write it on the agent.
 * @param timeout_ms    Timeout, ms.
 *
 * @return Status code.
 */
extern te_errno tapi_sdr_replay_record(tapi_job_factory_t *factory,
                                       const tapi_sdr_replay_opt *opt,
                                       const char *path, int timeout_ms);

/**
 * Read a recording and report what a decoder made of it.
 *
 * Drives @c rtl_433, which knows several hundred of these devices.
 * Nothing recognised is not an error: it means the decoder does not
 * know this device, which is common and is why
 * tapi_sdr_replay_compare() can also work on the samples themselves.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  opt          For the sample rate the file was made at.
 * @param[in]  path         The file on the agent.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] events       Vector of #tapi_sdr_event; release with
 *                          tapi_sdr_events_free().
 *
 * @return Status code.
 */
extern te_errno tapi_sdr_decode(tapi_job_factory_t *factory,
                                const tapi_sdr_replay_opt *opt,
                                const char *path, int timeout_ms,
                                te_vec *events);

/**
 * Compare two recordings of the same action and report what it means.
 *
 * Puts nothing on the air. If a decoder reads the same code out of
 * both, the device repeats itself and anything that recorded it once
 * can impersonate it for ever.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  opt          For the sample rate.
 * @param[in]  first        First recording on the agent.
 * @param[in]  second       Second recording of the same action.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] report       Report to append findings to.
 *
 * @return Status code of comparing, not the verdict.
 */
extern te_errno tapi_sdr_replay_compare(tapi_job_factory_t *factory,
                                        const tapi_sdr_replay_opt *opt,
                                        const char *first,
                                        const char *second,
                                        int timeout_ms,
                                        tapi_cybersec_report *report);

/**
 * Send a recording back out.
 *
 * The demonstration rather than the finding. Needs a radio that can
 * transmit, and needs the rig to be a cable or a shielded box.
 *
 * @param factory       Job factory.
 * @param opt           Frequency and rate to send at.
 * @param path          The recording on the agent.
 * @param timeout_ms    Timeout, ms.
 *
 * @return Status code.
 * @retval TE_EOPNOTSUPP    The radio cannot transmit.
 */
extern te_errno tapi_sdr_replay_send(tapi_job_factory_t *factory,
                                     const tapi_sdr_replay_opt *opt,
                                     const char *path, int timeout_ms);

/**
 * Write decoded events into the log.
 *
 * @param what          A word for the log.
 * @param events        Vector of #tapi_sdr_event.
 */
extern void tapi_sdr_events_log(const char *what, const te_vec *events);

/**
 * Release decoded events.
 *
 * @param events        Vector of #tapi_sdr_event.
 */
extern void tapi_sdr_events_free(te_vec *events);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_SDR_REPLAY_H__ */

/**@} <!-- END tapi_sdr_replay --> */

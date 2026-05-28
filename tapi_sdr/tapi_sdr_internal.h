/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief SDR TAPI: internal helpers
 *
 * Internal to tsf-sdr; not installed.
 */

#ifndef __TSF_TAPI_SDR_INTERNAL_H__
#define __TSF_TAPI_SDR_INTERNAL_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "te_vector.h"
#include "tapi_job.h"

#include "tapi_devtool_run.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Append one argument to a vector, taking ownership of it. */
extern void tapi_sdr_arg(te_vec *args, const char *fmt, ...)
    TE_LIKE_PRINTF(2, 3);

/**
 * Run a radio tool and hand back what it said.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  program      The tool.
 * @param[in]  args         Arguments after @c argv[0].
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] out          Standard output, or @c NULL.
 * @param[out] err          Standard error, or @c NULL.
 * @param[out] exit_code    Exit status, or @c NULL.
 *
 * @return Status code of running the tool, not of the tool.
 */
extern te_errno tapi_sdr_cmd(tapi_job_factory_t *factory,
                             const char *program, const te_vec *args,
                             int timeout_ms, te_string *out,
                             te_string *err, int *exit_code);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_SDR_INTERNAL_H__ */

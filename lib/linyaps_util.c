/* SPDX-License-Identifier: MIT */

#include "linyaps_private.h"

/* ------------------------------------------------------------------ */
/* String / memory helpers                                             */
/* ------------------------------------------------------------------ */

void free_string(char **s) {
  if (*s) {
    free(*s);
    *s = NULL;
  }
}

char *dupstr(const char *s) { return s ? strdup(s) : NULL; }

/* ------------------------------------------------------------------ */
/* LinyapsTaskProgress helpers                                          */
/* ------------------------------------------------------------------ */

void progress_clear(LinyapsTaskProgress *p) {
  free_string(&p->object_path);
  free_string(&p->message);
  memset(p, 0, sizeof(*p));
}

void progress_set_string(char **dst, const char *src) {
  free_string(dst);
  *dst = dupstr(src);
}

/* ------------------------------------------------------------------ */
/* Package list helpers                                                 */
/* ------------------------------------------------------------------ */

void package_list_append(LinyapsPackageInfo ***items, size_t *count,
                         size_t *cap, LinyapsPackageInfo *info) {
  if (*count == *cap) {
    size_t next = *cap ? *cap * 2 : 16;
    LinyapsPackageInfo **tmp = realloc(*items, next * sizeof(*tmp));
    if (!tmp) {
      linyaps_package_info_free(info);
      return;
    }
    *items = tmp;
    *cap = next;
  }
  (*items)[(*count)++] = info;
}

/* ------------------------------------------------------------------ */
/* Interaction helpers                                                  */
/* ------------------------------------------------------------------ */

void linyaps_interaction_request_clear(LinyapsInteractionRequest *request) {
  free(request->object_path);
  free(request->local_ref);
  free(request->remote_ref);
  free(request->summary);
  free(request->body);
  memset(request, 0, sizeof(*request));
}

/* ------------------------------------------------------------------ */
/* Error notification helper                                           */
/* ------------------------------------------------------------------ */

void notify_failed(LinyapsProgressCallback cb, void *userdata,
                   const char *message, int code) {
  if (!cb) {
    return;
  }
  LinyapsTaskProgress p = {0};
  p.state = LINYAPS_TASK_STATE_FAILED;
  p.percentage = 0.0;
  p.message = (char *)(message ? message : "operation failed");
  p.error_code = code ? code : -1;
  cb(&p, userdata);
}

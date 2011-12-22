
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas Tonnesen(andreto@olsr.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#ifndef _OLSR_PLUGIN_LOADER
#define _OLSR_PLUGIN_LOADER

#include "olsrd_plugin.h"
#include "olsr_types.h"
#include "olsr_cfg.h"

#ifndef OLSR_PLUGIN

/* all */
typedef int (*plugin_init_func) (void);
typedef int (*get_interface_version_func) (void);

#if SUPPORT_OLD_PLUGIN_VERSIONS

/* version 4 */
typedef int (*register_param_func) (char *, char *);
#endif

/* version 5 */
typedef void (*get_plugin_parameters_func) (const struct olsrd_plugin_parameters ** params, unsigned int *size);

struct olsr_plugin {
  /* The handle */
  void *dlhandle;

  struct plugin_param *params;
  int plugin_interface_version;

#if SUPPORT_OLD_PLUGIN_VERSIONS
  /* version 4 */
  register_param_func register_param;
#endif
  plugin_init_func plugin_init;

  /* version 5 */
  const struct olsrd_plugin_parameters *plugin_parameters;
  unsigned int plugin_parameters_size;

  struct olsr_plugin *next;
};

void olsr_load_plugins(void);

void olsr_close_plugins(void);

int olsr_plugin_io(int, void *, size_t);

#endif
#endif

/*
 * Local Variables:
 * mode: c
 * style: linux
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

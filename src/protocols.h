
/* Etherape
 * Copyright (C) 2000 Juan Toledo
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "prot_types.h"

#define LINESIZE 1024

/* Enums */

enum rpc_type
{
  RPC_CALL = 0,
  RPC_REPLY = 1
};

enum rpc_program
{
  BOOTPARAMS_PROGRAM = 1,
  MOUNT_PROGRAM = 100005,
  NFS_PROGRAM = 100003,
  NLM_PROGRAM = 100021,
  PORTMAP_PROGRAM = 100000,
  STAT_PROGRAM = 100024,
  YPBIND_PROGRAM = 100007,
  YPSERV_PROGRAM = 100004,
  YPXFR_PROGRAM = 100069
};

/* Variables */

static GTree *tcp_services = NULL;
static GTree *udp_services = NULL;
static guint offset = 0;

/* Functions declarations */

static void get_eth_type (void);
static void get_fddi_type (void);
static void get_eth_II (etype_t etype);

static void get_ip (void);
static void get_tcp (void);
static gint tcp_compare (gconstpointer a, gconstpointer b);
static void get_udp (void);
static gint udp_compare (gconstpointer a, gconstpointer b);
static gboolean get_rpc (void);
static void load_services (void);

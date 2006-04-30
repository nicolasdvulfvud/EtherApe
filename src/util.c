/* This is pretty messy because it is pretty much copied as is from 
 * ethereal. I should probably clean it up some day */


/* util.c
 * Utility routines
 *
 * $Id$
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@zing.org>
 * Copyright 1998 Gerald Combs
 *
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gnome.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef NEED_SNPRINTF_H
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include "snprintf.h"
#endif

#ifndef WIN32
#include <pwd.h>
#endif

#ifdef NEED_MKSTEMP
#include "mkstemp.h"
#endif

#include "globals.h"
#include "util.h"

#ifdef HAVE_IO_H
#include <io.h>
typedef int mode_t;		/* for win32 */
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#include <pcap.h> /*JTC*/

/* uncomment to enable the older method of getting an interface list */
/* #define OLDER_INTERFACE_LIST */



static void interface_list_free_cb(gpointer data, gpointer user_data);

#ifndef OLDER_INTERFACE_LIST

/* use only pcap to obtain the interface list */

GList *
interface_list_create(GString *err_str)
{
  GList *il = NULL;
  pcap_if_t *pcap_devlist = NULL;
  pcap_if_t *curdev;
  char pcap_errstr[1024]="";

  g_string_assign(err_str, "");

  if (pcap_findalldevs(&pcap_devlist, pcap_errstr) < 0)
    {
      /* can't obtain interface list from pcap */
      g_string_printf (err_str, "Getting interface list from pcap failed: %s",
	       pcap_errstr);
      return NULL;
    }

  /* We want to list the interfaces in order, but with loopbacks last. Since
   * glist_append must iterate over all elements (!!!), we use g_list_prepend
   * then reverse the list (stupid Glist!) */
    
  /* iterate on all pcap devices, skipping loopbacks*/
  for (curdev = pcap_devlist ; curdev ; curdev = curdev->next)
    {
      if (PCAP_IF_LOOPBACK == curdev->flags)
        continue; /* skip loopback */

      il = g_list_prepend(il, g_strdup(curdev->name));
    }

  /* loopbacks added last */
  for (curdev = pcap_devlist ; curdev ; curdev = curdev->next)
    {
      if (PCAP_IF_LOOPBACK != curdev->flags)
        continue; /* only loopback */

      il = g_list_prepend(il, g_strdup(curdev->name));
    }

  /* reverse list*/
  il = g_list_reverse(il);

  /* release pcap list */
  pcap_freealldevs(pcap_devlist);

  /* return ours */
  return il;
}


#else /* OLDER_INTERFACE_LIST */

  /* use the older method of obtaining the interface list */

static gint interface_list_search_cb(const gchar *a, const gchar *b);

/* JTC This define I copied from a different file from ethereal */
#define MIN_PACKET_SIZE 68	/* minimum amount of packet data we can read */

GList *
interface_list_create(GString *err_str)
{
  GList *il = NULL;
  gint nonloopback_pos = 0;
  struct ifreq *ifr, *last;
  struct ifconf ifc;
  struct ifreq ifrflags;
  int sock = socket (AF_INET, SOCK_DGRAM, 0);
  pcap_t *pch;
  char pcap_errstr[1024];

  g_string_assign(err_str, "");
  
  if (sock < 0)
    {
      g_string_printf (err_str, "Error opening socket: %s", strerror (errno));
      return NULL;
    }

  /*
   * Since we have to grab the interface list all at once, we'll
   * make plenty of room.
   */
  ifc.ifc_len = 1024 * sizeof (struct ifreq);
  ifc.ifc_buf = malloc (ifc.ifc_len);

  if (ioctl (sock, SIOCGIFCONF, &ifc) < 0 ||
      ifc.ifc_len < sizeof (struct ifreq))
    {
      g_string_printf (err_str, "SIOCGIFCONF error getting list of interfaces: %s",
	       strerror (errno));
      goto fail;
    }

  ifr = (struct ifreq *) ifc.ifc_req;
  last = (struct ifreq *) ((char *) ifr + ifc.ifc_len);
  while (ifr < last)
    {
      /*
       * Skip addresses that begin with "dummy", or that include
       * a ":" (the latter are Solaris virtuals).
       */
      if (strncmp (ifr->ifr_name, "dummy", 5) == 0 ||
	  strchr (ifr->ifr_name, ':') != NULL)
        {
	  g_my_debug(_("skipping dummy/virtual interface %s"), ifr->ifr_name);
	  goto next;
        }

      /*
       * If we already have this interface name on the list,
       * don't add it (SIOCGIFCONF returns, at least on
       * BSD-flavored systems, one entry per interface *address*;
       * if an interface has multiple addresses, we get multiple
       * entries for it).
       */
      if (g_list_find_custom(il, ifr->ifr_name, (GCompareFunc)interface_list_search_cb))
        {
	  g_my_debug(_("skipping already listed interface %s"), ifr->ifr_name);
	  goto next;
        }

      /*
       * Get the interface flags.
       */
      memset (&ifrflags, 0, sizeof ifrflags);
      strncpy (ifrflags.ifr_name, ifr->ifr_name, sizeof ifrflags.ifr_name);
      if (ioctl (sock, SIOCGIFFLAGS, (char *) &ifrflags) < 0)
	{
	  if (errno == ENXIO)
            {
	      g_my_debug(_("skipping interface %s: got ENXIO"), ifr->ifr_name);
	      goto next;
            }
	  g_string_printf (err_str,
		   "SIOCGIFFLAGS error getting flags for interface %s: %s",
		   ifr->ifr_name, strerror (errno));
	  goto fail;
	}

      /*
       * Skip interfaces that aren't up.
       */
      if (!(ifrflags.ifr_flags & IFF_UP))
        {
	  g_my_debug(_("skipping interface %s: is down"), ifr->ifr_name);
	  goto next;
        }

      /*
       * Skip interfaces that we can't open with "libpcap".
       * Open with the minimum packet size - it appears that the
       * IRIX SIOCSNOOPLEN "ioctl" may fail if the capture length
       * supplied is too large, rather than just truncating it.
       */
      pch = pcap_open_live (ifr->ifr_name, MIN_PACKET_SIZE, 0, 0, pcap_errstr);
      if (pch == NULL)
        {
	  g_my_debug(_("skipping interface %s: can't be opened with libpcap (error %s)\n"), ifr->ifr_name, pcap_errstr);
	  goto next;
        }
      pcap_close (pch);

      /*
       * If it's a loopback interface, add it at the end of the
       * list, otherwise add it after the last non-loopback
       * interface, so all loopback interfaces go at the end - we
       * don't want a loopback interface to be the default capture
       * device unless there are no non-loopback devices.
       */
      if ((ifrflags.ifr_flags & IFF_LOOPBACK) ||
	  strncmp (ifr->ifr_name, "lo", 2) == 0)
	il = g_list_insert (il, g_strdup (ifr->ifr_name), -1);
      else
	{
	  il = g_list_insert (il, g_strdup (ifr->ifr_name), nonloopback_pos);
	  /*
	   * Insert the next non-loopback interface after this
	   * one.
	   */
	  nonloopback_pos++;
	}

    next:
#ifdef HAVE_SA_LEN
      ifr = (struct ifreq *) ((char *) ifr + ifr->ifr_addr.sa_len + IFNAMSIZ);
#else
      ifr = (struct ifreq *) ((char *) ifr + sizeof (struct ifreq));
#endif
    }

  free (ifc.ifc_buf);
  close (sock);

  return il;

fail:
  interface_list_free(il);
  free (ifc.ifc_buf);
  close (sock);
  return NULL;
}

static gint
interface_list_search_cb(const gchar *a, const gchar *b)
{
  return strcmp(a, b);
}

#endif /* OLDER_INTERFACE_LIST */


static void
interface_list_free_cb(gpointer data, gpointer user_data)
{
  g_free (data);
}

void
interface_list_free(GList * if_list)
{
  if (if_list)
    {
      g_list_foreach (if_list, interface_list_free_cb, NULL);
      g_list_free (if_list);
    }
}


const char *
get_home_dir (void)
{
  char *env_value;
  static const char *home = NULL;
#ifndef WIN32
  struct passwd *pwd;
#endif

  /* Return the cached value, if available */
  if (home)
    return home;

  env_value = getenv ("HOME");

  if (env_value)
    {
      home = env_value;
    }
  else
    {
#ifdef WIN32
      /* XXX - on NT, get the user name and append it to
         "C:\winnt\profiles\"?
         What about Windows 9x? */
      home = "C:";
#else
      pwd = getpwuid (getuid ());
      if (pwd != NULL)
	{
	  /* This is cached, so we don't need to worry
	     about allocating multiple ones of them. */
	  home = g_strdup (pwd->pw_dir);
	}
      else
	home = "/tmp";
#endif
    }

  return home;
}

/* safe strncpy */
char *
safe_strncpy (char *dst, const char *src, size_t maxlen)
{
  
if (maxlen < 1)
    return dst;
  strncpy (dst, src, maxlen - 1);	/* no need to copy that last char */
  dst[maxlen - 1] = '\0';
  return dst;
}

/* safe strncat */
char *
safe_strncat (char *dst, const char *src, size_t maxlen)
{
  size_t lendst = strlen (dst);
  if (lendst >= maxlen)
    return dst;			/* already full, nothing to do */
  strncat (dst, src, maxlen - strlen (dst));
  dst[maxlen - 1] = '\0';
  return dst;
}

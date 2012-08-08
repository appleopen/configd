/*
 * Copyright (c) 2011 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */


#ifndef _NETWORK_INFORMATION_H_
#define _NETWORK_INFORMATION_H_

#include <stdint.h>

typedef struct _nwi_state * nwi_state_t;
typedef struct _nwi_ifstate * nwi_ifstate_t;

/*
 * Function: nwi_state_copy
 * Purpose:
 *   Returns the current network state information.
 *   Release after use by calling nwi_state_release().
 */
nwi_state_t
nwi_state_copy(void);

/*
 * Function: nwi_state_release
 * Purpose:
 *   Release the memory associated with the network state.
 */
void
nwi_state_release(nwi_state_t state);

/*
 * Function: nwi_state_get_notify_key
 * Purpose:
 *   Returns the BSD notify key to use to monitor when the state changes.
 *
 * Note:
 *   The nwi_state_copy API uses this notify key to monitor when the state
 *   changes, so each invocation of nwi_state_copy returns the current
 *   information.
 */
const char *
nwi_state_get_notify_key(void);

/*
 * Function: nwi_state_get_first_ifstate
 * Purpose:
 *   Returns the first and highest priority interface that has connectivity
 *   for the specified address family 'af'. 'af' is either AF_INET or AF_INET6.
 *   The connectivity provided is for general networking.   To get information
 *   about an interface that isn't available for general networking, use
 *   nwi_state_get_ifstate().
 *
 *   Use nwi_ifstate_get_next() to get the next, lower priority interface
 *   in the list.
 *
 *   Returns NULL if no connectivity for the specified address family is
 *   available.
 */
nwi_ifstate_t
nwi_state_get_first_ifstate(nwi_state_t state, int af);

/*
 * Function: nwi_state_get_generation
 * Purpose:
 *   Returns the generation (mach_time) of the nwi_state data.
 *   Every time the data is updated due to changes
 *   in the network, this value will change.
 */
uint64_t
nwi_state_get_generation(nwi_state_t state);

/*
 * Function: nwi_state_get_ifstate
 * Purpose:
 *   Return information for the specified interface 'ifname'.
 *
 *   This API directly returns the ifstate for the specified interface.
 *   This is the only way to access information about an interface that isn't
 *   available for general networking.
 *
 *   Returns NULL if no information is available for that interface.
 */
nwi_ifstate_t
nwi_state_get_ifstate(nwi_state_t state, const char * ifname);

/*
 * Function: nwi_ifstate_get_ifname
 * Purpose:
 *   Return the interface name of the specified ifstate.
 */
const char *
nwi_ifstate_get_ifname(nwi_ifstate_t ifstate);

/*
 * Type: nwi_ifstate_flags
 * Purpose:
 *   Provide information about the interface, including its IPv4 and IPv6
 *   connectivity, and whether DNS is configured or not.
 */
#define NWI_IFSTATE_FLAGS_HAS_IPV4	0x1	/* has IPv4 connectivity */
#define NWI_IFSTATE_FLAGS_HAS_IPV6	0x2	/* has IPv6 connectivity */
#define NWI_IFSTATE_FLAGS_HAS_DNS	0x4	/* has DNS configured */

typedef uint64_t nwi_ifstate_flags;
/*
 * Function: nwi_ifstate_get_flags
 * Purpose:
 *   Return the flags for the given ifstate (see above for bit definitions).
 */
nwi_ifstate_flags
nwi_ifstate_get_flags(nwi_ifstate_t ifstate);

/*
 * Function: nwi_ifstate_get_next
 * Purpose:
 *   Returns the next, lower priority nwi_ifstate_t after the specified
 *   'ifstate' for the protocol family 'af'.
 *
 *   Returns NULL when the end of the list is reached.
 */
nwi_ifstate_t
nwi_ifstate_get_next(nwi_ifstate_t ifstate, int af);

/*
 * Function: nwi_ifstate_compare_rank
 * Purpose:
 *   Compare the relative rank of two nwi_ifstate_t objects.
 *
 *   The "rank" indicates the importance of the underlying interface.
 *
 * Returns:
 *   0 	if ifstate1 and ifstate2 are ranked equally
 *  -1	if ifstate1 is ranked ahead of ifstate2
 *   1	if ifstate2 is ranked ahead of ifstate1
 */
int
nwi_ifstate_compare_rank(nwi_ifstate_t ifstate1, nwi_ifstate_t ifstate2);

/*
 * Function: _nwi_state_ack
 * Purpose:
 *   Acknowledge receipt and any changes associated with the [new or
 *   updated] network state.
 */
void
_nwi_state_ack(nwi_state_t state, const char *bundle_id)
	__OSX_AVAILABLE_STARTING(__MAC_10_8, __IPHONE_6_0);

#endif